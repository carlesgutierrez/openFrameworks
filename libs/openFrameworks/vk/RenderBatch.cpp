#include "vk/RenderBatch.h"
#include "vk/spooky/SpookyV2.h"
#include "vk/Shader.h"

using namespace of::vk;

// ------------------------------------------------------------

RenderBatch::RenderBatch( Context & rc )
	:mRenderContext( &rc ){
	mVkCmd = mRenderContext->allocateCommandBuffer( ::vk::CommandBufferLevel::ePrimary );
	

}

// ------------------------------------------------------------

of::vk::RenderBatch& RenderBatch::draw( const DrawCommand& dc_ ){

	// local copy of draw command.
	DrawCommand dc = dc_;

	// Commit draw command memory to gpu
	// This will update dynamic offsets as a side-effect, 
	// and will also update the buffer ID for the bindings affected.
	dc.commitUniforms( mRenderContext->getAllocator() );
	dc.commitMeshAttributes( mRenderContext->getAllocator() );
	
	// Renderpass is constant over a context, as a context encapsulates 
	// a renderpass with all its subpasses.
	dc.mPipelineState.setRenderPass( mRenderContext->getRenderPass() );
	
	dc.mPipelineState.setSubPass( mVkSubPassId );

	mDrawCommands.emplace_back( std::move(dc) );
	
	return *this;
}

// ----------------------------------------------------------------------

void RenderBatch::begin(){
	mVkCmd.begin( { ::vk::CommandBufferUsageFlagBits::eOneTimeSubmit } );

	{	// begin renderpass - 

		// this is only allowed if the context maps a primary renderpass!

		// CONSIDER: can we extract this into a renderpass object?
		// The goal being that a renderbatch maps to a renderpass, 
		// but a context may have multiple renderpasses, and may render
		// to multiple framebuffers.
		//
		// Note that secondary command buffers inherit the renderpass 
		// from their primary.

		::vk::RenderPassBeginInfo renderPassBeginInfo;
		renderPassBeginInfo
			.setRenderPass( mRenderContext->getRenderPass() )
			.setFramebuffer( mRenderContext->getFramebuffer() )
			.setRenderArea( mRenderContext->getRenderArea() )
			.setClearValueCount( mRenderContext->getClearValues().size() )
			.setPClearValues( mRenderContext->getClearValues().data() )
			;

		mVkCmd.beginRenderPass( renderPassBeginInfo, ::vk::SubpassContents::eInline );
	}

	// set dynamic viewport
	// todo: these dynamics belong to the batch state.
	::vk::Viewport vp;
	vp.setX( 0 )
		.setY( 0 )
		.setWidth( mRenderContext->getRenderArea().extent.width )
		.setHeight( mRenderContext->getRenderArea().extent.height )
		.setMinDepth( 0.f )
		.setMaxDepth( 1.f )
		;
	mVkCmd.setViewport( 0, { vp } );
	mVkCmd.setScissor( 0, { mRenderContext->getRenderArea() } );
}

// ----------------------------------------------------------------------

void RenderBatch::end(){
	// submit command buffer to context.
	// context will submit command buffers batched to queue 
	// at its own pleasure, but in seqence.

	processDrawCommands();

	{
		// end renderpass if Context / CommandBuffer is Primary
		mVkCmd.endRenderPass();
	}

	mVkCmd.end();
	
	// add command buffer to command queue of context.
	mRenderContext->submit( std::move( mVkCmd ) );

	mVkCmd = nullptr;
	mDrawCommands.clear();
}

// ----------------------------------------------------------------------

void RenderBatch::processDrawCommands( ){

	// first order draw commands

	// order them by 
	// 1) subpass id, 
	// 2) pipeline,
	// 3) descriptor set usage

	// then process draw commands
	
	// current draw state for building command buffer - this is based on parsing the drawCommand list
	std::unique_ptr<GraphicsPipelineState> boundPipelineState;


	for ( auto & dc : mDrawCommands ){

		// find out pipeline state needed for this draw command

		if ( !boundPipelineState || *boundPipelineState != dc.mPipelineState ){
			// look up pipeline in pipeline cache
			// otherwise, create a new pipeline, then bind pipeline.

			boundPipelineState = std::make_unique<GraphicsPipelineState>( dc.mPipelineState );

			uint64_t pipelineStateHash = boundPipelineState->calculateHash();

			auto & currentPipeline = mRenderContext->borrowPipeline( pipelineStateHash );

			if ( currentPipeline.get() == nullptr ){
				currentPipeline  =
					std::shared_ptr<::vk::Pipeline>( ( new ::vk::Pipeline ),
						[device = mRenderContext->mDevice]( ::vk::Pipeline*rhs ){
					if ( rhs ){
						device.destroyPipeline( *rhs );
					}
					delete rhs;
				} );

				*currentPipeline = boundPipelineState->createPipeline( mRenderContext->mDevice, mRenderContext->mSettings.pipelineCache);
			}

			mVkCmd.bindPipeline( ::vk::PipelineBindPoint::eGraphics, *currentPipeline );
		}

		// ----------| invariant: correct pipeline is bound

		// Match currently bound DescriptorSetLayouts against 
		// dc pipeline DescriptorSetLayouts
		std::vector<::vk::DescriptorSet> boundVkDescriptorSets;
		std::vector<uint32_t> dynamicBindingOffsets;

		const std::vector<uint64_t> & setLayoutKeys = dc.mPipelineState.getShader()->getDescriptorSetLayoutKeys();

		for ( size_t setId = 0; setId != setLayoutKeys.size(); ++setId ){

			uint64_t setLayoutKey = setLayoutKeys[setId];
			auto & descriptors = dc.getDescriptorSetData( setId ).descriptors;
			const auto descriptorSetLayout = dc.mPipelineState.getShader()->getDescriptorSetLayout( setId );
			// calculate hash of descriptorset, combined with descriptor set sampler state
			uint64_t descriptorSetHash = SpookyHash::Hash64( descriptors.data(), descriptors.size() * sizeof( DescriptorSetData_t::DescriptorData_t ), setLayoutKey );

			// Receive a descriptorSet from the renderContext's cache.
			// The renderContext will allocate and initialise a DescriptorSet if none has been found.
			const ::vk::DescriptorSet& descriptorSet = mRenderContext->getDescriptorSet( descriptorSetHash, setId, *descriptorSetLayout , descriptors );

			boundVkDescriptorSets.emplace_back( descriptorSet );

			const auto & offsets  = dc.getDescriptorSetData( setId ).dynamicBindingOffsets;
			
			// now append dynamic binding offsets for this set to vector of dynamic offsets for this draw call
			dynamicBindingOffsets.insert( dynamicBindingOffsets.end(), offsets.begin(), offsets.end() );

		}

		// We always bind the full descriptor set.

		// bind dc descriptorsets to current pipeline descriptor sets
		// make sure dynamic ubos have the correct offsets
		if ( !boundVkDescriptorSets.empty() ){
			mVkCmd.bindDescriptorSets(
				::vk::PipelineBindPoint::eGraphics,	                           // use graphics, not compute pipeline
				*dc.mPipelineState.getShader()->getPipelineLayout(),           // VkPipelineLayout object used to program the bindings.
				0,                                                             // firstset: first set index (of the above) to bind to - mDescriptorSet[0] will be bound to pipeline layout [firstset]
				boundVkDescriptorSets.size(),                                  // setCount: how many sets to bind
				boundVkDescriptorSets.data(),                                  // the descriptor sets to match up with our mPipelineLayout (need to be compatible)
				dynamicBindingOffsets.size(),                                  // dynamic offsets count how many dynamic offsets
				dynamicBindingOffsets.data()                                   // dynamic offsets for each descriptor
			);
		}


		{

			const auto & vertexOffsets = dc.getVertexOffsets();
			const auto & indexOffset   = dc.getIndexOffsets();

			const auto & vertexBuffers = dc.getVertexBuffers();
			const auto & indexBuffer   = dc.getIndexBuffer();

			//// Store vertex data using Context.
			//// - this uses Allocator to store mesh data in the current frame' s dynamic memory
			//// Context will return memory offsets into vertices, indices, based on current context memory buffer
			//// 
			// CONSIDER: check if it made sense to cache already stored meshes, 
			////       so that meshes which have already been stored this frame 
			////       may be re-used.
			//storeMesh( mesh_, vertexOffsets, indexOffsets );

			// CONSIDER: cull vertexOffsets which refer to empty vertex attribute data
			//       make sure that a pipeline with the correct bindings is bound to match the 
			//       presence or non-presence of mesh data.

			// Bind vertex data buffers to current pipeline. 
			// The vector indices into bufferRefs, vertexOffsets correspond to [binding numbers] of the currently bound pipeline.
			// See Shader.h for an explanation of how this is mapped to shader attribute locations

			if ( !vertexBuffers.empty() ){
				mVkCmd.bindVertexBuffers( 0, vertexBuffers, vertexOffsets );
			}

			if ( !indexBuffer ){
				// non-indexed draw
				mVkCmd.draw( uint32_t( dc.getNumVertices() ), 1, 0, 0 ); //last param was 1
			} else{
				// indexed draw
				mVkCmd.bindIndexBuffer( indexBuffer, indexOffset, ::vk::IndexType::eUint32 );
				mVkCmd.drawIndexed( dc.getNumIndices(), 1, 0, 0, 0 ); // last param was 1
			}
		}

	}


}

