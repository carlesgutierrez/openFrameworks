#pragma once

#include <memory>
#include "ofLog.h"
#include "vk/Pipeline.h"
#include "vk/vkAllocator.h"
#include "vk/DrawCommand.h"

/*

MISSION: 

	A RenderContext needs to be able to live within its own thread - 
	A RenderContext needs to have its own pools, 
	and needs to be thread-safe.

	One or more batches may submit into a rendercontext - the render-
	context will accumulate vkCommandbuffers, and will submit them 
	on submitDraw.

	A RenderContext is the OWNER of all elements used to draw within 
	one thread.

*/

class ofVkRenderer; // ffdecl.

namespace of{
namespace vk{

class RenderBatch; // ffdecl.

struct TransferSrcData
{
	void * pData;
	::vk::DeviceSize numElements;
	::vk::DeviceSize numBytesPerElement;
};

struct BufferRegion
{
	::vk::Buffer buffer = nullptr;
	::vk::DeviceSize offset = 0;
	::vk::DeviceSize range = VK_WHOLE_SIZE;
	uint64_t numElements = 0;
};

// ------------------------------------------------------------

class RenderContext
{
	friend RenderBatch;
public:
	struct Settings
	{
		ofVkRenderer *                         renderer = nullptr;
		Allocator::Settings                    transientMemoryAllocatorSettings;
		std::shared_ptr<::vk::PipelineCache>   pipelineCache;
		::vk::RenderPass                       renderPass;  // owning
		::vk::Rect2D                           renderArea;
	};

private:

	const Settings mSettings;
	const ::vk::Device&                         mDevice = mSettings.transientMemoryAllocatorSettings.device;

	struct VirtualFrame
	{
		::vk::CommandPool                       commandPool;
		::vk::QueryPool                         queryPool;
		::vk::Framebuffer                       frameBuffer;
		std::list<::vk::DescriptorPool>         descriptorPools;
		std::map<uint64_t, ::vk::DescriptorSet> descriptorSetCache;
		::vk::Semaphore                         semaphoreImageAcquired;
		::vk::Semaphore                         semaphoreRenderComplete;
		::vk::Fence                             fence;
		std::vector<::vk::CommandBuffer>        commandBuffers;
	};

	std::vector<VirtualFrame>                   mVirtualFrames;
	size_t                                      mCurrentVirtualFrame = 0;
	
	// Renderpass with subpasses for this context. from which framebuffers are derived.
	// each context has their own renderpass object, from which framebuffers are partly derived.
	const ::vk::RenderPass &                    mRenderPass = mSettings.renderPass; 
	uint32_t                                    mSubpassId  = 0;

	std::unique_ptr<of::vk::Allocator>          mTransientMemory;

	// Max number of descriptors per type
	// Array index == descriptor type
	std::array<uint32_t, VK_DESCRIPTOR_TYPE_RANGE_SIZE> mDescriptorPoolSizes;

	// Number of descriptors left available for allocation from mDescriptorPool.
	// Array index == descriptor type
	std::array<uint32_t, VK_DESCRIPTOR_TYPE_RANGE_SIZE> mAvailableDescriptorCounts;

	// Max number of sets which can be allocated from the main per-frame descriptor pool
	uint32_t mDescriptorPoolMaxSets = 0;

	// Bitfield indicating whether the descriptor pool for a virtual frame is dirty 
	// Each bit represents a virtual frame index. 
	// We're not expecting more than 64 virtual frames (more than 3 seldom make sense)
	uint64_t mDescriptorPoolsDirty = 0; // -1 == all bits '1' == all dirty

	// Re-consolidate descriptor pools if necessary
	void updateDescriptorPool();

	// Fetch descriptor either from cache - or allocate and initialise a descriptor based on DescriptorSetData.
	const ::vk::DescriptorSet getDescriptorSet( uint64_t descriptorSetHash, size_t setId, const DrawCommand & drawCommand );

	// cache for all pipelines ever used within this context
	std::map<uint64_t, std::shared_ptr<::vk::Pipeline>>    mPipelineCache;
	
	const ::vk::Rect2D&                mRenderArea = mSettings.renderArea;
	
	void resetFence();

	std::shared_ptr<::vk::Pipeline>& borrowPipeline( uint64_t pipelineHash ){
		return mPipelineCache[pipelineHash];
	};
	
	const std::unique_ptr<Allocator> & RenderContext::getAllocator();
	

public:

	RenderContext( const Settings&& settings );
	~RenderContext();


	const ::vk::Fence       & getFence() const ;
	const ::vk::Semaphore   & getImageAcquiredSemaphore() const ;
	const ::vk::Semaphore   & getSemaphoreRenderComplete() const ;
	const ::vk::Framebuffer & getFramebuffer() const;
	const ::vk::RenderPass  & getRenderPass() const; 
	
	const uint32_t            getSubpassId() const;

	void setupFrameBufferAttachments( const std::vector<::vk::ImageView> &attachments);

	// Stages data for copying into targetAllocator's address space
	// allocates identical memory chunk in local transient allocator and in targetAllocator
	// use BufferCopy vec and a vkCmdBufferCopy to execute copy instruction using a command buffer.
	std::vector<::vk::BufferCopy> stageData( const std::vector<TransferSrcData>& dataVec, const unique_ptr<Allocator> &targetAllocator );

	std::vector<BufferRegion> storeDataCmd( const std::vector<TransferSrcData>& dataVec, const unique_ptr<Allocator> &targetAllocator );

	// Create and return command buffer. 
	// Lifetime is limited to current frame. 
	// It *must* be submitted to this context within the same frame, that is, before swap().
	::vk::CommandBuffer requestAndBeginPrimaryCommandBuffer();

	::vk::CommandBuffer allocateTransientCommandBuffer( const ::vk::CommandBufferLevel & commandBufferLevel );

	const std::unique_ptr<of::vk::Allocator>& getTransientAllocator(){
		return mTransientMemory;
	};

	const ::vk::Device & getDevice() const{
		return mDevice;
	};

	void setRenderArea( const ::vk::Rect2D& renderArea );
	const ::vk::Rect2D & getRenderArea() const;

	void setup();
	void begin();
	
	// move command buffer to the rendercontext for batched submission
	void submit( ::vk::CommandBuffer&& commandBuffer );
	
	void submitDraw();
	// void submitTransfer();
	void swap();

};

// ------------------------------------------------------------

inline void RenderContext::submit(::vk::CommandBuffer && commandBuffer) {
	mVirtualFrames.at( mCurrentVirtualFrame ).commandBuffers.emplace_back(std::move(commandBuffer));
}


inline const ::vk::Fence & RenderContext::getFence() const {
	return mVirtualFrames.at( mCurrentVirtualFrame ).fence;
}

inline const ::vk::Semaphore & RenderContext::getImageAcquiredSemaphore() const {
	return mVirtualFrames.at( mCurrentVirtualFrame ).semaphoreImageAcquired;
}

inline const ::vk::Semaphore & RenderContext::getSemaphoreRenderComplete() const {
	return mVirtualFrames.at( mCurrentVirtualFrame ).semaphoreRenderComplete;
}

inline const ::vk::Framebuffer & RenderContext::getFramebuffer() const{
	return mVirtualFrames[ mCurrentVirtualFrame ].frameBuffer;
}

inline const ::vk::RenderPass & RenderContext::getRenderPass() const{
	return mSettings.renderPass;
}

inline const uint32_t RenderContext::getSubpassId() const{
	return mSubpassId;
}

inline void RenderContext::setRenderArea( const::vk::Rect2D & renderArea_ ){
	const_cast<::vk::Rect2D&>( mSettings.renderArea ) = renderArea_;
}

inline const ::vk::Rect2D & RenderContext::getRenderArea() const{
	return mRenderArea;
}

inline const std::unique_ptr<Allocator> & RenderContext::getAllocator(){
	return mTransientMemory;
}

// ------------------------------------------------------------

inline std::vector<::vk::BufferCopy> RenderContext::stageData( const std::vector<TransferSrcData>& dataVec, const unique_ptr<Allocator>& targetAllocator )
{
	std::vector<::vk::BufferCopy> regions;
	regions.reserve( dataVec.size());
	
	for ( auto & src : dataVec ){
		::vk::BufferCopy region { 0, 0, 0 };
		
		region.size = src.numBytesPerElement * src.numElements;

		void * pData;
		if (    targetAllocator->allocate( region.size, region.dstOffset )
			&& mTransientMemory->allocate( region.size, region.srcOffset )
			&& mTransientMemory->map( pData )
			){
			
			memcpy( pData, src.pData, region.size );

			regions.push_back(std::move(region));
		} else {
			ofLogError() << "StageData: alloc error";
			break;
		}
	}

	return regions;
}

// ------------------------------------------------------------

inline std::vector<BufferRegion> RenderContext::storeDataCmd( const std::vector<TransferSrcData>& dataVec, const unique_ptr<Allocator>& targetAllocator ){
	std::vector<BufferRegion> resultBuffers;

	auto copyRegions = stageData( dataVec, targetAllocator );
	resultBuffers.reserve( copyRegions.size() );

	const auto targetBuffer = targetAllocator->getBuffer();

	for ( size_t i = 0; i != copyRegions.size(); ++i ){
		const auto &region  = copyRegions[i];
		const auto &srcData = dataVec[i];
		BufferRegion bufRegion;
		bufRegion.buffer      = targetBuffer;
		bufRegion.numElements = srcData.numElements;
		bufRegion.offset      = region.dstOffset;
		bufRegion.range       = region.size;
		resultBuffers.push_back( std::move( bufRegion ) );
	}

	::vk::DeviceSize firstOffset = copyRegions.front().dstOffset;
	::vk::DeviceSize totalStaticRange = ( copyRegions.back().dstOffset + copyRegions.back().size ) - firstOffset;

	::vk::CommandBuffer cmd = allocateTransientCommandBuffer(::vk::CommandBufferLevel::ePrimary);

	cmd.begin( { ::vk::CommandBufferUsageFlagBits::eOneTimeSubmit } );
	{

		cmd.copyBuffer( getTransientAllocator()->getBuffer(), targetAllocator->getBuffer(), copyRegions );

		::vk::BufferMemoryBarrier bufferTransferBarrier;
		bufferTransferBarrier
			.setSrcAccessMask( ::vk::AccessFlagBits::eTransferWrite )  // not sure if these are optimal.
			.setDstAccessMask( ::vk::AccessFlagBits::eShaderRead )    // not sure if these are optimal.
			.setSrcQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
			.setDstQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
			.setBuffer( targetAllocator->getBuffer() )
			.setOffset( firstOffset )
			.setSize( totalStaticRange )
			;

		// Add pipeline barrier so that transfers must have completed 
		// before next command buffer will start executing.

		cmd.pipelineBarrier(
			::vk::PipelineStageFlagBits::eTopOfPipe,
			::vk::PipelineStageFlagBits::eTopOfPipe,
			::vk::DependencyFlagBits(),
			{}, /* no fence */
			{ bufferTransferBarrier }, /* buffer barriers */
			{}                         /* image barriers */
		);
	}
	cmd.end();

	// Submit copy command buffer to current context
	// This needs to happen before first draw calls are submitted for the frame.
	submit( std::move( cmd ) );
	
	return resultBuffers;
}

// ------------------------------------------------------------

inline ::vk::CommandBuffer RenderContext::requestAndBeginPrimaryCommandBuffer(){
	::vk::CommandBuffer cmd;

	::vk::CommandBufferAllocateInfo commandBufferAllocateInfo;
	commandBufferAllocateInfo
		.setCommandPool( mVirtualFrames[mCurrentVirtualFrame].commandPool )
		.setLevel( ::vk::CommandBufferLevel::ePrimary )
		.setCommandBufferCount( 1 )
		;

	mDevice.allocateCommandBuffers( &commandBufferAllocateInfo, &cmd );

	cmd.begin( { ::vk::CommandBufferUsageFlagBits::eOneTimeSubmit } );

	{	// begin renderpass
		//! TODO: get correct clear values, and clear value count
		std::array<::vk::ClearValue, 2> clearValues;
		clearValues[0].setColor( reinterpret_cast<const ::vk::ClearColorValue&>( ofFloatColor::blueSteel ) );
		clearValues[1].setDepthStencil( { 1.f, 0 } );

		::vk::RenderPassBeginInfo renderPassBeginInfo;
		renderPassBeginInfo
			.setRenderPass( getRenderPass() )
			.setFramebuffer(getFramebuffer() )
			.setRenderArea( getRenderArea() )
			.setClearValueCount( uint32_t(clearValues.size()) )
			.setPClearValues( clearValues.data() )
			;

		cmd.beginRenderPass( renderPassBeginInfo, ::vk::SubpassContents::eInline );
	}

	return cmd;
}

// ------------------------------------------------------------

inline ::vk::CommandBuffer RenderContext::allocateTransientCommandBuffer(
	const ::vk::CommandBufferLevel & commandBufferLevel = ::vk::CommandBufferLevel::ePrimary  ){
	::vk::CommandBuffer cmd;

	::vk::CommandBufferAllocateInfo commandBufferAllocateInfo;
	commandBufferAllocateInfo
		.setCommandPool( mVirtualFrames[mCurrentVirtualFrame].commandPool )
		.setLevel( commandBufferLevel )
		.setCommandBufferCount( 1 )
		;

	mDevice.allocateCommandBuffers( &commandBufferAllocateInfo, &cmd );

	return cmd;
}

}  // end namespace of::vk
}  // end namespace of