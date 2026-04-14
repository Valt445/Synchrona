// ============================================================

#define CGLTF_IMPLEMENTATION
#include "loader.h"
#include "engine.h"

// STB_IMAGE_IMPLEMENTATION defined in stb_image_impl.cpp
#include <stb_image.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <chrono>
#include <fstream>     // ← NEW: for DDS file loading

// ─── Timing ───────────────────────────────────────────────────────────────────
static double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// ─── Direct buffer access helpers ────────────────────────────────────────────
static const uint8_t* acc_base(const cgltf_accessor* a) {
    return (const uint8_t*)a->buffer_view->buffer->data
        + a->buffer_view->offset + a->offset;
}
static size_t acc_stride(const cgltf_accessor* a) {
    return a->stride ? a->stride : cgltf_calc_size(a->type, a->component_type);
}
static glm::vec2 read_v2(const uint8_t* b, size_t s, size_t i) {
    const float* p = (const float*)(b + i * s);
    return { p[0], p[1] };
}
static glm::vec3 read_v3(const uint8_t* b, size_t s, size_t i) {
    const float* p = (const float*)(b + i * s);
    return { p[0], p[1], p[2] };
}
static glm::vec4 read_v4(const uint8_t* b, size_t s, size_t i) {
    const float* p = (const float*)(b + i * s);
    return { p[0], p[1], p[2], p[3] };
}

// ─── Node local transform ─────────────────────────────────────────────────────
static glm::mat4 node_local(const cgltf_node* n) {
    if (n->has_matrix) {
        glm::mat4 m;
        memcpy(&m, n->matrix, sizeof(float) * 16);
        return m;
    }
    glm::mat4 T(1.0f), R(1.0f), S(1.0f);
    if (n->has_translation)
        T = glm::translate(glm::mat4(1.0f), { n->translation[0], n->translation[1], n->translation[2] });
    if (n->has_rotation) {
        glm::quat q(n->rotation[3], n->rotation[0], n->rotation[1], n->rotation[2]);
        R = glm::mat4_cast(glm::normalize(q));
    }
    if (n->has_scale)
        S = glm::scale(glm::mat4(1.0f), { n->scale[0], n->scale[1], n->scale[2] });
    return T * R * S;
}


// ============================================================================
// DDS / BC TEXTURE SUPPORT
// ============================================================================

#pragma pack(push, 1)
struct DDSPixelFormat {
    uint32_t size, flags, fourCC, RGBBitCount;
    uint32_t RBitMask, GBitMask, BBitMask, ABitMask;
};
struct DDSHeader {
    uint32_t size, flags, height, width;
    uint32_t pitchOrLinearSize, depth, mipMapCount;
    uint32_t reserved1[11];
    DDSPixelFormat ddspf;
    uint32_t caps, caps2, caps3, caps4, reserved2;
};
struct DDSHeaderDXT10 {
    uint32_t dxgiFormat, resourceDimension, miscFlag, arraySize, miscFlags2;
};
#pragma pack(pop)

static constexpr uint32_t DDS_MAGIC = 0x20534444; // "DDS "
static constexpr uint32_t DDPF_FOURCC = 0x4;
static constexpr uint32_t FOURCC_DX10 = 0x30315844; // "DX10"
static constexpr uint32_t FOURCC_DXT1 = 0x31545844; // "DXT1"
static constexpr uint32_t FOURCC_DXT3 = 0x33545844; // "DXT3"
static constexpr uint32_t FOURCC_DXT5 = 0x35545844; // "DXT5"
static constexpr uint32_t FOURCC_BC4U = 0x55344342; // "BC4U"
static constexpr uint32_t FOURCC_BC4S = 0x53344342; // "BC4S"
static constexpr uint32_t FOURCC_ATI2 = 0x32495441; // "ATI2" = BC5
static constexpr uint32_t FOURCC_BC5U = 0x55354342; // "BC5U"

// DXGI format → VkFormat
static VkFormat dxgi_to_vk(uint32_t dxgi) {
    switch (dxgi) {
    case 71:  return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
    case 72:  return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
    case 74:  return VK_FORMAT_BC2_UNORM_BLOCK;
    case 75:  return VK_FORMAT_BC2_SRGB_BLOCK;
    case 77:  return VK_FORMAT_BC3_UNORM_BLOCK;
    case 78:  return VK_FORMAT_BC3_SRGB_BLOCK;
    case 80:  return VK_FORMAT_BC4_UNORM_BLOCK;
    case 81:  return VK_FORMAT_BC4_SNORM_BLOCK;
    case 83:  return VK_FORMAT_BC5_UNORM_BLOCK;
    case 84:  return VK_FORMAT_BC5_SNORM_BLOCK;
    case 95:  return VK_FORMAT_BC6H_UFLOAT_BLOCK;
    case 96:  return VK_FORMAT_BC6H_SFLOAT_BLOCK;
    case 98:  return VK_FORMAT_BC7_UNORM_BLOCK;
    case 99:  return VK_FORMAT_BC7_SRGB_BLOCK;
    default:  return VK_FORMAT_UNDEFINED;
    }
}

// Bytes per 4×4 block for each BC format
static uint32_t bc_bytes_per_block(VkFormat fmt) {
    switch (fmt) {
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VK_FORMAT_BC4_UNORM_BLOCK:
    case VK_FORMAT_BC4_SNORM_BLOCK:
        return 8;
    default:
        return 16;
    }
}

// Byte size of one mip level of a BC-compressed image
static uint32_t bc_mip_size(uint32_t baseW, uint32_t baseH, uint32_t mip, uint32_t bpb) {
    uint32_t w = std::max(1u, baseW >> mip);
    uint32_t h = std::max(1u, baseH >> mip);
    uint32_t blocksX = std::max(1u, (w + 3) / 4);
    uint32_t blocksY = std::max(1u, (h + 3) / 4);
    return blocksX * blocksY * bpb;
}

struct DDSData {
    VkFormat             format = VK_FORMAT_UNDEFINED;
    uint32_t             width = 0;
    uint32_t             height = 0;
    uint32_t             mipLevels = 1;
    std::vector<uint8_t> data;      // raw compressed bytes, all mips concatenated
};

static bool load_dds_file(const std::filesystem::path& path, DDSData& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;

    size_t fileSize = (size_t)f.tellg();
    f.seekg(0);

    // Magic
    uint32_t magic = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    if (magic != DDS_MAGIC) {
        std::cerr << "[DDS] Bad magic in " << path << "\n";
        return false;
    }

    // Main header (124 bytes)
    DDSHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(DDSHeader));
    if (hdr.size != 124) {
        std::cerr << "[DDS] Bad header size in " << path << "\n";
        return false;
    }

    out.width = hdr.width;
    out.height = hdr.height;
    out.mipLevels = std::max(1u, hdr.mipMapCount);

    bool hasDX10 = (hdr.ddspf.flags & DDPF_FOURCC) &&
        (hdr.ddspf.fourCC == FOURCC_DX10);

    if (hasDX10) {
        // Extended DX10 header — this is what texconv outputs
        DDSHeaderDXT10 dx10{};
        f.read(reinterpret_cast<char*>(&dx10), sizeof(DDSHeaderDXT10));
        out.format = dxgi_to_vk(dx10.dxgiFormat);
    }
    else {
        // Legacy FourCC (older DDS files)
        if (hdr.ddspf.flags & DDPF_FOURCC) {
            switch (hdr.ddspf.fourCC) {
            case FOURCC_DXT1: out.format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK; break;
            case FOURCC_DXT3: out.format = VK_FORMAT_BC2_UNORM_BLOCK;       break;
            case FOURCC_DXT5: out.format = VK_FORMAT_BC3_UNORM_BLOCK;       break;
            case FOURCC_BC4U: out.format = VK_FORMAT_BC4_UNORM_BLOCK;       break;
            case FOURCC_BC4S: out.format = VK_FORMAT_BC4_SNORM_BLOCK;       break;
            case FOURCC_ATI2:
            case FOURCC_BC5U: out.format = VK_FORMAT_BC5_UNORM_BLOCK;       break;
            default:
                std::cerr << "[DDS] Unknown FourCC 0x" << std::hex
                    << hdr.ddspf.fourCC << std::dec << " in " << path << "\n";
                return false;
            }
        }
        else {
            std::cerr << "[DDS] No FourCC and no DX10 header in " << path << "\n";
            return false;
        }
    }

    if (out.format == VK_FORMAT_UNDEFINED) {
        std::cerr << "[DDS] Unsupported DXGI format in " << path << "\n";
        return false;
    }

    
    size_t dataStart = (size_t)f.tellg();
    size_t dataSize = fileSize - dataStart;
    out.data.resize(dataSize);
    f.read(reinterpret_cast<char*>(out.data.data()), (std::streamsize)dataSize);

    return true;
}


static void upload_compressed_image(Engine* e, AllocatedImage& image, const DDSData& dds)
{
    uint32_t bpb = bc_bytes_per_block(dds.format);

    // Build one VkBufferImageCopy per mip level
    std::vector<VkBufferImageCopy> regions;
    regions.reserve(dds.mipLevels);
    uint32_t bufOffset = 0;

    for (uint32_t mip = 0; mip < dds.mipLevels; ++mip) {
        uint32_t mipW = std::max(1u, dds.width >> mip);
        uint32_t mipH = std::max(1u, dds.height >> mip);
        uint32_t size = bc_mip_size(dds.width, dds.height, mip, bpb);

        VkBufferImageCopy region{};
        region.bufferOffset = bufOffset;
        region.bufferRowLength = 0;   // tightly packed
        region.bufferImageHeight = 0;
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1 };
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { mipW, mipH, 1 };
        regions.push_back(region);

        bufOffset += size;
    }

    // Staging buffer — one allocation for all mips
    AllocatedBuffer staging = create_buffer(e->allocator,
        dds.data.size(),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY, e);

    void* mapped = nullptr;
    vmaMapMemory(e->allocator, staging.allocation, &mapped);
    memcpy(mapped, dds.data.data(), dds.data.size());
    vmaUnmapMemory(e->allocator, staging.allocation);

    immediate_submit([&](VkCommandBuffer cmd)
        {
            VkImageSubresourceRange fullRange{};
            fullRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            fullRange.baseMipLevel = 0;
            fullRange.levelCount = VK_REMAINING_MIP_LEVELS;
            fullRange.baseArrayLayer = 0;
            fullRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

            // UNDEFINED → TRANSFER_DST
            VkImageMemoryBarrier toTransfer{};
            toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.srcAccessMask = 0;
            toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toTransfer.image = image.image;
            toTransfer.subresourceRange = fullRange;

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &toTransfer);

            // Copy all mip levels in a single call
            vkCmdCopyBufferToImage(cmd,
                staging.buffer,
                image.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                (uint32_t)regions.size(),
                regions.data());

            // TRANSFER_DST → SHADER_READ_ONLY (all mips at once)
            VkImageMemoryBarrier toShader = toTransfer;
            toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &toShader);

        }, e);

    destroy_buffer(staging, e);
}

// ============================================================================
// END DDS SUPPORT
// ============================================================================


void generate_mipmaps(Engine* e, VkCommandBuffer cmd, VkImage img,
    uint32_t mipLevels, int32_t width, int32_t height)
{
    for (int32_t i = 1; i < (int32_t)mipLevels; ++i)
    {
        // ── transition mip i-1: TRANSFER_DST → TRANSFER_SRC ─────────────────
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = img;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT,
                                        (uint32_t)(i - 1), 1, 0, 1 }; // ONE mip only
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // ── blit ─────────────────────────────────────────────────────────────
        VkImageBlit blit{};
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)(i - 1), 0, 1 };
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { width >> (i - 1), height >> (i - 1), 1 };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)i, 0, 1 };
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { width >> i, height >> i, 1 };

        // clamp to 1 so we never get a 0-size mip
        blit.srcOffsets[1].x = std::max(blit.srcOffsets[1].x, 1);
        blit.srcOffsets[1].y = std::max(blit.srcOffsets[1].y, 1);
        blit.dstOffsets[1].x = std::max(blit.dstOffsets[1].x, 1);
        blit.dstOffsets[1].y = std::max(blit.dstOffsets[1].y, 1);

        vkCmdBlitImage(cmd,
            img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        // ── transition mip i-1: TRANSFER_SRC → SHADER_READ — done forever ───
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // ── final mip never got transitioned in the loop — do it now ─────────────
    VkImageMemoryBarrier finalBarrier{};
    finalBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    finalBarrier.image = img;
    finalBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    finalBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    finalBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT,
                                         mipLevels - 1, 1, 0, 1 };
    finalBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    finalBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    finalBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    finalBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &finalBarrier);
}

void upload_image_data(Engine* e, AllocatedImage& image, const void* pixels, size_t size)
{
    if (!pixels || size == 0) return;

    // --- Staging buffer (CPU-visible) ----------------------------------------
    AllocatedBuffer staging = create_buffer(e->allocator, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY, e);

    void* mapped = nullptr;
    vmaMapMemory(e->allocator, staging.allocation, &mapped);
    memcpy(mapped, pixels, size);
    vmaUnmapMemory(e->allocator, staging.allocation);

    // --- Record and submit ---------------------------------------------------
    immediate_submit([&](VkCommandBuffer cmd)
        {
            // Cover ALL mip levels so no level is left in UNDEFINED layout.
            VkImageSubresourceRange fullRange{};
            fullRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            fullRange.baseMipLevel = 0;
            fullRange.levelCount = VK_REMAINING_MIP_LEVELS;
            fullRange.baseArrayLayer = 0;
            fullRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

            // Transition entire image: UNDEFINED → TRANSFER_DST
            VkImageMemoryBarrier toTransfer{};
            toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toTransfer.srcAccessMask = 0;
            toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toTransfer.image = image.image;
            toTransfer.subresourceRange = fullRange;

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &toTransfer);

            // Copy pixel data into mip level 0 only.
            VkBufferImageCopy copy{};
            copy.bufferOffset = 0;
            copy.bufferRowLength = 0;   // tightly packed
            copy.bufferImageHeight = 0;
            copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            copy.imageOffset = { 0, 0, 0 };
            copy.imageExtent = image.imageExtent;

            vkCmdCopyBufferToImage(cmd, staging.buffer, image.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

            // Generate mip chain via blit
            generate_mipmaps(e, cmd, image.image,
                image.mipLevels,
                (int32_t)image.imageExtent.width,
                (int32_t)image.imageExtent.height);

        }, e);

    // Safe to destroy now — immediate_submit waited on the fence.
    destroy_buffer(staging, e);
}

// ─── load_image_from_gltf ─────────────────────────────────────────────────────
// NOW: checks for a .dds sibling file first. If found, loads BC-compressed
// data with pre-baked mip chain (no blit needed). Falls back to stbi otherwise.
AllocatedImage load_image_from_gltf(Engine* e, cgltf_image* img, bool isLinear)
{
    if (!img) return {};

    // ── 1. Try DDS (BC-compressed, pre-baked mips) ────────────────────────────
    // Only possible for external textures — embedded GLB textures have no path.
    if (img->uri) {
        std::filesystem::path srcPath = e->sceneBasePath / img->uri;
        std::filesystem::path ddsPath = srcPath;
        ddsPath.replace_extension(".dds");

        if (std::filesystem::exists(ddsPath)) {
            DDSData dds;
            if (load_dds_file(ddsPath, dds)) {
                // texconv outputs UNORM by default even for sRGB textures when using
                // BC7_UNORM_SRGB — the DX10 header already encodes the correct sRGB
                // format (dxgi=99). But if the user compressed with BC7_UNORM only
                // and isLinear=false, promote to the sRGB variant so hardware
                // linearisation still works correctly in the sampler.
                if (!isLinear) {
                    if (dds.format == VK_FORMAT_BC1_RGB_UNORM_BLOCK)  dds.format = VK_FORMAT_BC1_RGB_SRGB_BLOCK;
                    else if (dds.format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK) dds.format = VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
                    else if (dds.format == VK_FORMAT_BC2_UNORM_BLOCK)      dds.format = VK_FORMAT_BC2_SRGB_BLOCK;
                    else if (dds.format == VK_FORMAT_BC3_UNORM_BLOCK)      dds.format = VK_FORMAT_BC3_SRGB_BLOCK;
                    else if (dds.format == VK_FORMAT_BC7_UNORM_BLOCK)      dds.format = VK_FORMAT_BC7_SRGB_BLOCK;
                }

                VkExtent3D extent{ dds.width, dds.height, 1 };

                // Create image with BC format. Pass true so create_image allocates
                // the full mip chain based on dimensions — matches what texconv -m 0 produces.
                // We use TRANSFER_DST_BIT only (no TRANSFER_SRC needed — no blit generation).
                AllocatedImage gpu = create_image(e, extent, dds.format,
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    true);
                gpu.imageExtent = extent;

                upload_compressed_image(e, gpu, dds);

                std::cout << "  [DDS BC] " << ddsPath.filename().string()
                    << "  " << dds.width << "x" << dds.height
                    << "  mips=" << dds.mipLevels
                    << "  fmt=" << (int)dds.format << "\n";
                return gpu;
            }
            // If DDS load failed for any reason, fall through to stbi
            std::cerr << "[loader] DDS load failed for " << ddsPath
                << " — falling back to uncompressed\n";
        }
    }

    // ── 2. Fallback: stbi (uncompressed RGBA8) ────────────────────────────────
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = nullptr;

    if (img->uri) {
        // External texture
        std::filesystem::path fullPath = e->sceneBasePath / img->uri;
        pixels = stbi_load(fullPath.string().c_str(), &width, &height, &channels, 4);
        if (!pixels) {
            std::cerr << "[loader] Failed to load external texture: "
                << fullPath << " — " << stbi_failure_reason() << "\n";
            return {};
        }
    }
    else if (img->buffer_view) {
        // Embedded texture (GLB or base64 GLTF).
        const uint8_t* raw = (const uint8_t*)img->buffer_view->buffer->data
            + img->buffer_view->offset;
        size_t rawSize = img->buffer_view->size;
        pixels = stbi_load_from_memory(raw, (int)rawSize, &width, &height, &channels, 4);
        if (!pixels) {
            std::cerr << "[loader] Embedded texture decode failed — "
                << stbi_failure_reason() << "\n";
            return {};
        }
    }

    if (!pixels) return {};

    VkFormat format = isLinear ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
    VkExtent3D extent{ (uint32_t)width, (uint32_t)height, 1 };

    AllocatedImage gpu = create_image(
        e,
        extent,
        format,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        true
    );

    gpu.imageExtent = extent;

    upload_image_data(e, gpu, pixels, (size_t)width * height * 4);
    stbi_image_free(pixels);

    return gpu;
}

// ─── Texture registry ─────────────────────────────────────────────────────────
using TexMap = std::unordered_map<const cgltf_image*, uint32_t>;

static uint32_t get_or_upload(
    Engine* e,
    TexMap& map,
    const cgltf_image* img,
    bool isLinear)
{
    if (!img)
        return INVALID_TEXTURE;

    auto it = map.find(img);
    if (it != map.end())
        return it->second;

    AllocatedImage gpu = load_image_from_gltf(
        e,
        const_cast<cgltf_image*>(img),
        isLinear
    );

    if (gpu.image == VK_NULL_HANDLE)
        return INVALID_TEXTURE;

    uint32_t slot = e->nextBindlessTextureIndex++;
    upload_texture_to_bindless(e, gpu, e->defaultSamplerLinear, slot);
    e->sceneTextures.push_back(gpu);
    map[img] = slot;

    return slot;
}

static uint32_t resolve(
    Engine* e,
    TexMap& map,
    const cgltf_texture_view& tv,
    bool isLinear)
{
    if (!tv.texture || !tv.texture->image)
        return INVALID_TEXTURE;

    return get_or_upload(e, map, tv.texture->image, isLinear);
}

// ─── Primitive loader ─────────────────────────────────────────────────────────
static bool load_primitive(
    const cgltf_primitive* prim,
    const glm::mat4& localTransform,
    std::vector<Vertex>& verts,
    std::vector<uint32_t>& idx,
    GeoSurface& surf)
{
    size_t vcount = 0;
    for (size_t a = 0; a < prim->attributes_count; ++a) {
        if (prim->attributes[a].type == cgltf_attribute_type_position) {
            vcount = prim->attributes[a].data->count;
            break;
        }
    }
    if (vcount == 0) return false;

    surf.startIndex = (uint32_t)idx.size();
    uint32_t vtxBase = (uint32_t)verts.size();

    if (prim->indices) {
        surf.count = (uint32_t)prim->indices->count;
        for (size_t i = 0; i < prim->indices->count; ++i)
            idx.push_back((uint32_t)cgltf_accessor_read_index(prim->indices, i) + vtxBase);
    }
    else {
        surf.count = (uint32_t)vcount;
        for (uint32_t i = 0; i < (uint32_t)vcount; ++i)
            idx.push_back(vtxBase + i);
    }

    verts.resize(vtxBase + vcount);
    for (size_t i = 0; i < vcount; ++i)
        verts[vtxBase + i].color = glm::vec4(1.0f);


    for (size_t a = 0; a < prim->attributes_count; ++a) {
        const cgltf_attribute* attr = &prim->attributes[a];
        const cgltf_accessor* acc = attr->data;
        if (!acc->buffer_view || !acc->buffer_view->buffer->data) continue;

        const uint8_t* buf = acc_base(acc);
        const size_t   stride = acc_stride(acc);

        switch (attr->type) {
        case cgltf_attribute_type_position:
            for (size_t i = 0; i < vcount; ++i)
                verts[vtxBase + i].position = read_v3(buf, stride, i);
            break;

        case cgltf_attribute_type_normal:
            for (size_t i = 0; i < vcount; ++i)
                verts[vtxBase + i].normal = read_v3(buf, stride, i);
            break;
        case cgltf_attribute_type_texcoord:
            if (attr->index == 0)
                for (size_t i = 0; i < vcount; ++i)
                    verts[vtxBase + i].uv = read_v2(buf, stride, i);
            break;
        case cgltf_attribute_type_color:
            for (size_t i = 0; i < vcount; ++i) {
                if (acc->type == cgltf_type_vec4)
                    verts[vtxBase + i].color = read_v4(buf, stride, i);
                else {
                    glm::vec3 c = read_v3(buf, stride, i);
                    verts[vtxBase + i].color = glm::vec4(c, 1.0f);
                }
            }
            break;
        case cgltf_attribute_type_tangent:
            for (size_t i = 0; i < vcount; ++i)
                verts[vtxBase + i].tangent = read_v4(buf, stride, i);
            break;
        default: break;
        }
    }
    return true;
}

// ─── Recursive node traversal ─────────────────────────────────────────────────
static void traverse_node(
    const cgltf_node* node,
    const glm::mat4& parentWorld,
    Engine* e,
    TexMap& texMap,
    std::vector<std::shared_ptr<MeshAsset>>& out)
{
    if (!node) return;

    glm::mat4 localT = node_local(node);
    glm::mat4 worldT = parentWorld * localT;

    if (node->mesh) {
        const cgltf_mesh* mesh = node->mesh;

        size_t totalV = 0, totalI = 0;
        for (size_t pi = 0; pi < mesh->primitives_count; ++pi) {
            const cgltf_primitive* p = &mesh->primitives[pi];
            if (p->type != cgltf_primitive_type_triangles) continue;
            if (p->indices) totalI += p->indices->count;
            for (size_t a = 0; a < p->attributes_count; ++a) {
                if (p->attributes[a].type == cgltf_attribute_type_position) {
                    totalV += p->attributes[a].data->count;
                    break;
                }
            }
        }

        if (totalV > 0) {
            MeshAsset asset;
            asset.name = node->name ? node->name : "unnamed";
            asset.worldTransform = worldT;

            std::vector<Vertex>   verts;
            std::vector<uint32_t> indices;
            verts.reserve(totalV);
            indices.reserve(totalI ? totalI : totalV);

            for (size_t pi = 0; pi < mesh->primitives_count; ++pi) {
                const cgltf_primitive* prim = &mesh->primitives[pi];
                if (prim->type != cgltf_primitive_type_triangles) continue;

                GeoSurface surf{};

                if (prim->material) {
                    const cgltf_material* mat = prim->material;
                    const auto& pbr = mat->pbr_metallic_roughness;

                    surf.albedoIndex = resolve(e, texMap, pbr.base_color_texture, false);
                    surf.metallicRoughnessIndex = resolve(e, texMap, pbr.metallic_roughness_texture, true);
                    surf.normalIndex = resolve(e, texMap, mat->normal_texture, true);
                    surf.aoIndex = resolve(e, texMap, mat->occlusion_texture, true);
                    surf.emissiveIndex = resolve(e, texMap, mat->emissive_texture, false);

                    surf.colorFactor = glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1],
                        pbr.base_color_factor[2], pbr.base_color_factor[3]);
                    surf.metallicFactor = pbr.metallic_factor;
                    surf.roughnessFactor = pbr.roughness_factor;
                    surf.emissiveFactor = glm::vec3(mat->emissive_factor[0],
                        mat->emissive_factor[1],
                        mat->emissive_factor[2]);
                }

                if (load_primitive(prim, localT, verts, indices, surf))
                    asset.surfaces.push_back(surf);
            }

            if (!verts.empty() && !indices.empty()) {
                bool hasTangents = false;
                for (const auto& v : verts)
                    if (glm::length(glm::vec3(v.tangent)) > 0.001f) { hasTangents = true; break; }
                if (!hasTangents)
                    calculateTangents(verts, indices);

                asset.meshBuffers = uploadMesh(e, indices, verts);
                out.push_back(std::make_shared<MeshAsset>(std::move(asset)));
            }
        }
    }

    for (size_t i = 0; i < node->children_count; ++i)
        traverse_node(node->children[i], worldT, e, texMap, out);
}



// ─── Main entry point ─────────────────────────────────────────────────────────
std::optional<std::vector<std::shared_ptr<MeshAsset>>>
loadgltfMeshes(Engine* e, std::filesystem::path filePath)
{
    double t0 = now_ms();
    std::cout << "\n╔══════════════════════════════════════════════╗\n";
    std::cout << "║ " << filePath.filename().string() << "\n";
    std::cout << "╚══════════════════════════════════════════════╝\n";

    e->sceneBasePath = filePath.parent_path();

    cgltf_options opts{};
    cgltf_data* data = nullptr;

    if (cgltf_parse_file(&opts, filePath.string().c_str(), &data) != cgltf_result_success) {
        std::cerr << "[loader] ❌ Parse failed: " << filePath << "\n";
        return std::nullopt;
    }
    if (cgltf_load_buffers(&opts, data, filePath.string().c_str()) != cgltf_result_success) {
        std::cerr << "[loader] ❌ Buffer load failed: " << filePath << "\n";
        cgltf_free(data);
        return std::nullopt;
    }

    std::cout << " Meshes " << data->meshes_count
        << " | Materials " << data->materials_count
        << " | Textures " << data->textures_count << "\n";

    if (e->nextBindlessTextureIndex <= e->iblBrdfLutIndex)
        e->nextBindlessTextureIndex = e->iblBrdfLutIndex + 1;

    TexMap texMap;
    texMap.reserve(data->textures_count * 2);

    std::vector<std::shared_ptr<MeshAsset>> meshes;
    meshes.reserve(data->meshes_count);

    const cgltf_scene* scene = data->scene ? data->scene
        : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);

    if (scene) {
        for (size_t i = 0; i < scene->nodes_count; ++i)
            traverse_node(scene->nodes[i], glm::mat4(1.0f), e, texMap, meshes);
    }
    else {
        for (size_t i = 0; i < data->nodes_count; ++i)
            if (!data->nodes[i].parent)
                traverse_node(&data->nodes[i], glm::mat4(1.0f), e, texMap, meshes);
    }

    cgltf_free(data);

    size_t totalTris = 0;
    for (auto& m : meshes)
        for (auto& s : m->surfaces)
            totalTris += s.count / 3;

    std::cout << " ✅ " << meshes.size() << " mesh nodes | "
        << texMap.size() << " textures | "
        << totalTris << " triangles | "
        << (int)(now_ms() - t0) << " ms\n\n";

    return meshes;
}

// ─── upload_texture_to_bindless ───────────────────────────────────────────────
inline void upload_texture_to_bindless_safe(Engine* e, AllocatedImage img,
    VkSampler sampler, uint32_t index)
{
    if (img.image == VK_NULL_HANDLE) return;

    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler = sampler;
    imgInfo.imageView = img.imageView;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = e->bindlessSet;
    write.dstBinding = 0;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imgInfo;

    vkUpdateDescriptorSets(e->device, 1, &write, 0, nullptr);
}

inline void upload_texture_to_bindless(Engine* e, AllocatedImage img,
    VkSampler sampler, uint32_t index)
{
    upload_texture_to_bindless_safe(e, img, sampler, index);
}