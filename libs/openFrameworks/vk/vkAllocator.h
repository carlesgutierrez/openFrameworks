#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>

class ofVkRenderer; // ffdecl.

namespace of{
namespace vk{


class Allocator
{

public:
	struct Settings
	{
		::vk::DeviceSize                 size       = 0; // how much memory to reserve on hardware for this allocator
		ofVkRenderer                    *renderer   = nullptr;
		::vk::Device                     device     = nullptr;
		uint32_t                         frames     = 1; // number of frames to reserve within this allocator
	};

	Allocator( const Settings& settings )
	: mSettings(settings)
	{};

	~Allocator(){
		reset();
	};

	/// @detail set up allocator based on Settings and pre-allocate 
	///         a chunk of GPU memory, and attach a buffer to it 
	void setup();

	/// @brief  free GPU memory and de-initialise allocator
	void reset();

	/// @brief  sub-allocate a chunk of memory from GPU
	/// 
	bool allocate(::vk::DeviceSize byteCount_, void*& pAddr, ::vk::DeviceSize& offset, size_t frame_);
	
	/// @brief  remove all sub-allocations within the given frame
	/// @note   this does not free GPU memory, it just marks it as unused
	void free(size_t frame_);

	const ::vk::Buffer& getBuffer(){
		return mBuffer;
	};

private:
	const Settings                 mSettings;
	const ::vk::DeviceSize         mAlignment = 256;  // alignment is calculated on setup - but 256 is a sensible default as it is the largest possible according to spec

	std::vector<::vk::DeviceSize>  mOffsetEnd;        // next free location for allocations
	std::vector<uint8_t*>          mBaseAddress;      // base address for mapped memory

	::vk::Buffer                   mBuffer;			  // owning
	::vk::DeviceMemory             mDeviceMemory;	  // owning

};


} // namespace vk
} // namespace of
