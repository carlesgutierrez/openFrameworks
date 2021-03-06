#pragma once

#include "vk/Allocator.h"
#include "vk/HelperTypes.h"
namespace of{
namespace vk{

// ----------------------------------------------------------------------


/*
	BufferAllocator is a simple linear allocator.

	Allocator may have more then one virtual frame,
	and only allocations from the current virutal 
	frame are performed until swap(). 

	Allocator may be for transient memory or for 
	static memory.

	If allocated from Host memory, the allocator 
	maps a buffer to CPU visible memory for its 
	whole lifetime. 

*/


class BufferAllocator : public AbstractAllocator
{

public:

	struct Settings : public AbstractAllocator::Settings
	{
		uint32_t frameCount = 1; // number of frames to reserve within this allocator

		::vk::BufferUsageFlags bufferUsageFlags = (
			::vk::BufferUsageFlagBits::eIndexBuffer
			| ::vk::BufferUsageFlagBits::eUniformBuffer
			| ::vk::BufferUsageFlagBits::eVertexBuffer
			| ::vk::BufferUsageFlagBits::eTransferSrc
			| ::vk::BufferUsageFlagBits::eTransferDst );

		Settings & setSize( ::vk::DeviceSize size_ ){
			AbstractAllocator::Settings::size = size_;
			return *this;
		}
		Settings & setMemFlags( ::vk::MemoryPropertyFlags flags_ ){
			AbstractAllocator::Settings::memFlags = flags_;
			return *this;
		}
		Settings & setQueueFamilyIndices( const std::vector<uint32_t> indices_ ){
			AbstractAllocator::Settings::queueFamilyIndices = indices_;
			return *this;
		}
		Settings & setRendererProperties( const of::vk::RendererProperties& props ){
			AbstractAllocator::Settings::device = props.device;
			AbstractAllocator::Settings::physicalDeviceMemoryProperties = props.physicalDeviceMemoryProperties;
			AbstractAllocator::Settings::physicalDeviceProperties = props.physicalDeviceProperties;
			return *this;
		}
		Settings & setBufferUsageFlags( const ::vk::BufferUsageFlags& flags ){
			bufferUsageFlags = flags;
			return *this;
		}
		Settings & setFrameCount( uint32_t count_ ){
			frameCount = count_;
			return *this;
		}
	};

	BufferAllocator()
	: mSettings()
	{};

	~BufferAllocator(){
		mSettings.device.waitIdle();
		reset();
	};

	/// @detail set up allocator based on Settings and pre-allocate 
	///         a chunk of GPU memory, and attach a buffer to it 
	void setup(const BufferAllocator::Settings settings);

	/// @brief  free GPU memory and de-initialise allocator
	void reset() override;

	/// @brief  sub-allocate a chunk of memory from GPU
	/// 
	bool allocate( ::vk::DeviceSize byteCount_, ::vk::DeviceSize& offset ) override;

	void swap() override;

	const ::vk::DeviceMemory& getDeviceMemory() const override;

	/// @brief  remove all sub-allocations within the given frame
	/// @note   this does not free GPU memory, it just marks it as unused
	void free();

	// return address to writeable memory, if this allocator is ready to write.
	bool map( void*& pAddr ){
		pAddr = mCurrentMappedAddress;
		return ( mCurrentMappedAddress != nullptr );
	};

	// jump to use next segment assigned to next virtual frame

	const ::vk::Buffer& getBuffer() const{
		return mBuffer;
	};

	const AbstractAllocator::Settings& getSettings() const override{
		return mSettings;
	}

private:
	const BufferAllocator::Settings    mSettings;
	const ::vk::DeviceSize             mAlignment = 256;  // alignment is calculated on setup - but 256 is a sensible default as it is the largest possible according to spec

	std::vector<::vk::DeviceSize>      mOffsetEnd;        // next free location for allocations
	std::vector<uint8_t*>              mBaseAddress;      // base address for mapped memory

	::vk::Buffer                       mBuffer = nullptr;   // owning
	::vk::DeviceMemory                 mDeviceMemory = nullptr;	  // owning

	void*                              mCurrentMappedAddress = nullptr; // current address for writing
	size_t                             mCurrentVirtualFrameIdx = 0; // currently mapped segment
};

// ----------------------------------------------------------------------

inline const ::vk::DeviceMemory & of::vk::BufferAllocator::getDeviceMemory() const{
	return mDeviceMemory;
}

// ----------------------------------------------------------------------


} // namespace of::vk
} // namespace of