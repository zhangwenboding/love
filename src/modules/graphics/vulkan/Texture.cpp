#include "Texture.h"
#include "Graphics.h"

// make vulkan::Graphics functions available
#define vgfx ((Graphics*)gfx)

namespace love {
	namespace graphics {
		namespace vulkan {
			Texture::Texture(love::graphics::Graphics* gfx, const Settings& settings, const Slices* data)
				: love::graphics::Texture(gfx, settings, data), gfx(gfx) {
				allocator = vgfx->getVmaAllocator();
				device = vgfx->getDevice();

				if (data) {
					auto sliceData = data->get(0, 0);
					auto size = sliceData->getSize();
					auto dataPtr = sliceData->getData();
					Rect rect;
					rect.x = 0;
					rect.y = 0;
					rect.w = sliceData->getWidth();
					rect.h = sliceData->getHeight();

					uploadByteData(PIXELFORMAT_RGBA8_UNORM, dataPtr, size, 0, 0, rect);
				}
				else {
					uint8 defaultPixel[] = { 255, 255, 255, 255 };
					Rect rect = { 0, 0, 1, 1 };
					uploadByteData(PIXELFORMAT_RGBA8_UNORM, defaultPixel, sizeof(defaultPixel), 0, 0, rect);
				}
				createTextureImageView();
				createTextureSampler();
			}

			Texture::~Texture() {
				// vkDestroySampler(device, textureSampler, nullptr);
				// vkDestroyImageView(device, textureImageView, nullptr);
				// vmaDestroyImage(allocator, textureImage, nullptr);
			}

			void Texture::createTextureImageView() {
				VkImageViewCreateInfo viewInfo{};
				viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				viewInfo.image = textureImage;
				viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
				viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
				viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				viewInfo.subresourceRange.baseMipLevel = 0;
				viewInfo.subresourceRange.levelCount = 1;
				viewInfo.subresourceRange.baseArrayLayer = 0;
				viewInfo.subresourceRange.layerCount = 1;

				if (vkCreateImageView(device, &viewInfo, nullptr, &textureImageView) != VK_SUCCESS) {
					throw love::Exception("could not create texture image view");
				}
			}

			void Texture::createTextureSampler() {
				auto physicalDevice = vgfx->getPhysicalDevice();
				VkPhysicalDeviceProperties properties{};
				vkGetPhysicalDeviceProperties(physicalDevice, &properties);

				VkSamplerCreateInfo samplerInfo{};
				samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
				samplerInfo.magFilter = VK_FILTER_LINEAR;
				samplerInfo.minFilter = VK_FILTER_LINEAR;
				samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				samplerInfo.anisotropyEnable = VK_TRUE;
				samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
				samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
				samplerInfo.unnormalizedCoordinates = VK_FALSE;
				samplerInfo.compareEnable = VK_FALSE;
				samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
				samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
				samplerInfo.mipLodBias = 0.0f;
				samplerInfo.minLod = 0.0f;
				samplerInfo.maxLod = 0.0f;

				if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
					throw love::Exception("failed to create texture sampler");
				}
			}

			void Texture::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
				auto commandBuffer = vgfx->beginSingleTimeCommands();

				VkPipelineStageFlags sourceStage;
				VkPipelineStageFlags destinationStage;

				VkImageMemoryBarrier barrier{};
				barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barrier.oldLayout = oldLayout;
				barrier.newLayout = newLayout;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = image;
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				barrier.subresourceRange.baseMipLevel = 0;
				barrier.subresourceRange.levelCount = 1;
				barrier.subresourceRange.baseArrayLayer = 0;
				barrier.subresourceRange.layerCount = 1;

				if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
					barrier.srcAccessMask = 0;
					barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

					sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
					destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				} else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
					barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

					sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
					destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				}
				else {
					throw love::Exception("unsupported lay transition");
				}

				vkCmdPipelineBarrier(
					commandBuffer,
					sourceStage, destinationStage,
					0,
					0, nullptr,
					0, nullptr,
					1, &barrier
				);

				vgfx->endSingleTimeCommands(commandBuffer);
			}

			void Texture::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
				auto commandBuffer = vgfx->beginSingleTimeCommands();

				VkBufferImageCopy region{};
				region.bufferOffset = 0;
				region.bufferRowLength = 0;
				region.bufferImageHeight = 0;

				region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				region.imageSubresource.mipLevel = 0;
				region.imageSubresource.baseArrayLayer = 0;
				region.imageSubresource.layerCount = 1;

				region.imageOffset = { 0, 0, 0 };
				region.imageExtent = {
					width,
					height, 1
				};

				vkCmdCopyBufferToImage(
					commandBuffer,
					buffer,
					image,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1,
					&region
				);

				vgfx->endSingleTimeCommands(commandBuffer);
			}

			void Texture::uploadByteData(PixelFormat pixelformat, const void* data, size_t size, int level, int slice, const Rect& r) {
				VkBuffer stagingBuffer;
				VmaAllocation vmaAllocation;

				VkBufferCreateInfo bufferCreateInfo{};
				bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				bufferCreateInfo.size = size;
				bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

				VmaAllocationCreateInfo allocCreateInfo = {};
				allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
				allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

				VmaAllocationInfo allocInfo;
				vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &stagingBuffer, &vmaAllocation, &allocInfo);

				memcpy(allocInfo.pMappedData, data, size);

				VkImageCreateInfo imageInfo{};
				imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				imageInfo.imageType = VK_IMAGE_TYPE_2D;
				imageInfo.extent.width = static_cast<uint32_t>(r.w);
				imageInfo.extent.height = static_cast<uint32_t>(r.h);
				imageInfo.extent.depth = 1;
				imageInfo.mipLevels = 1;
				imageInfo.arrayLayers = 1;
				imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
				imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
				imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
				imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

				VmaAllocationCreateInfo imageAllocationCreateInfo{};

				if (vmaCreateImage(allocator, &imageInfo, &imageAllocationCreateInfo, &textureImage, &textureImageAllocation, nullptr) != VK_SUCCESS) {
					throw love::Exception("failed to create image");
				}

				transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
				copyBufferToImage(stagingBuffer, textureImage, static_cast<uint32_t>(r.w), static_cast<uint32_t>(r.h));
				transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

				vmaDestroyBuffer(allocator, stagingBuffer, vmaAllocation);
			}
		}
	}
}
