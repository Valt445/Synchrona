#include <raytrace.h>
#include <engine.h>
#include <vulkan/vulkan_raii.hpp> 

void init_acceleration_structure(Engine* e, std::vector<std::shared_ptr<MeshAsset>>& meshes) {
    // 1. Function Pointer Setup
    e->pfn_vkGetBuildSizes = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(e->device, "vkGetAccelerationStructureBuildSizesKHR");
    e->pfn_vkCreateAS = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(e->device, "vkCreateAccelerationStructureKHR");
    e->pfn_vkGetASAddress = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(e->device, "vkGetAccelerationStructureDeviceAddressKHR");
    e->pfn_vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(e->device, "vkCmdBuildAccelerationStructuresKHR");

    // Query scratch alignment requirement
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR
    };
    VkPhysicalDeviceProperties2 props2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &asProps
    };
    vkGetPhysicalDeviceProperties2(e->physicalDevice, &props2);
    uint64_t scratchAlignment = asProps.minAccelerationStructureScratchOffsetAlignment;

    struct BLASBuildTask {
        VkAccelerationStructureBuildGeometryInfoKHR buildInfo;
        VkAccelerationStructureBuildRangeInfoKHR range;
        AllocatedBuffer storage;
        VkAccelerationStructureKHR handle;
    };
    std::vector<BLASBuildTask> tasks;
    uint64_t maxScratchSize = 0;

    // --- PHASE 1: PREPARE ALL BLAS ---
    for (auto& mesh : meshes) {
        uint32_t triangleCount = mesh->meshBuffers.indexCount / 3;

        VkAccelerationStructureGeometryTrianglesDataKHR triangles{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
            .vertexData = {.deviceAddress = mesh->meshBuffers.vertexBufferAddress },
            .vertexStride = sizeof(Vertex),
            .maxVertex = mesh->meshBuffers.indexCount,
            .indexType = VK_INDEX_TYPE_UINT32,
            .indexData = {.deviceAddress = mesh->meshBuffers.indexBufferAddress },
        };

        VkAccelerationStructureGeometryKHR* geo = new VkAccelerationStructureGeometryKHR{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
            .geometry = {.triangles = triangles },
            .flags = VK_GEOMETRY_OPAQUE_BIT_KHR
        };

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
            .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .geometryCount = 1,
            .pGeometries = geo
        };

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        e->pfn_vkGetBuildSizes(e->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &triangleCount, &sizeInfo);

        maxScratchSize = std::max(maxScratchSize, sizeInfo.buildScratchSize);

        AllocatedBuffer blasStorage = create_buffer(e->allocator, sizeInfo.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY, e);

        VkAccelerationStructureCreateInfoKHR createInfo{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .buffer = blasStorage.buffer,
            .size = sizeInfo.accelerationStructureSize,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
        };

        VkAccelerationStructureKHR handle;
        e->pfn_vkCreateAS(e->device, &createInfo, nullptr, &handle);
        buildInfo.dstAccelerationStructure = handle;

        tasks.push_back({ buildInfo, {triangleCount, 0, 0, 0}, blasStorage, handle });
        mesh->blasHandle = handle;
    }

    // --- PHASE 2: QUERY TLAS SIZE EARLY so we can fold it into maxScratchSize ---
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    for (size_t i = 0; i < meshes.size(); i++) {
        VkAccelerationStructureInstanceKHR inst{};
        glm::mat4 transposed = glm::transpose(meshes[i]->worldTransform);
        memcpy(&inst.transform, &transposed, sizeof(inst.transform));
        inst.instanceCustomIndex = i;
        inst.mask = 0xFF;
        inst.accelerationStructureReference = meshes[i]->blasAddress;
        inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instances.push_back(inst);
    }

    uint32_t instanceCount = static_cast<uint32_t>(instances.size());

    VkAccelerationStructureGeometryInstancesDataKHR instancesData{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .data = {.deviceAddress = 0 } // placeholder, real address filled in later
    };
    VkAccelerationStructureGeometryKHR tlasGeo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry = {.instances = instancesData }
    };
    VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = &tlasGeo
    };

    VkAccelerationStructureBuildSizesInfoKHR tlasSizeInfo{ .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    e->pfn_vkGetBuildSizes(e->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlasBuildInfo, &instanceCount, &tlasSizeInfo);

    // NOW fold TLAS scratch into maxScratchSize before creating the scratch buffer
    maxScratchSize = std::max(maxScratchSize, tlasSizeInfo.buildScratchSize);

    // --- PHASE 3: CREATE SCRATCH BUFFER with alignment padding ---
    AllocatedBuffer globalScratch = create_buffer(e->allocator,
        maxScratchSize + scratchAlignment, // extra room to align address upward
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY, e);

    VkBufferDeviceAddressInfo scratchAddrInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = globalScratch.buffer
    };
    VkDeviceAddress rawScratchAddr = vkGetBufferDeviceAddress(e->device, &scratchAddrInfo);
    VkDeviceAddress alignedScratchAddr = (rawScratchAddr + scratchAlignment - 1) & ~(VkDeviceAddress)(scratchAlignment - 1);

    // --- PHASE 4: EXECUTE BLAS BUILDS ---
    immediate_submit([&](VkCommandBuffer cmd) {
        for (auto& task : tasks) {
            task.buildInfo.scratchData.deviceAddress = alignedScratchAddr;
            const VkAccelerationStructureBuildRangeInfoKHR* pRange = &task.range;
            e->pfn_vkCmdBuildAccelerationStructuresKHR(cmd, 1, &task.buildInfo, &pRange);

            VkMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
            };
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                0, 1, &barrier, 0, nullptr, 0, nullptr);
        }
        }, e);

    // Get BLAS addresses now that they're built
    for (size_t i = 0; i < tasks.size(); i++) {
        VkAccelerationStructureDeviceAddressInfoKHR addrInfo{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
            .accelerationStructure = tasks[i].handle
        };
        meshes[i]->blasAddress = e->pfn_vkGetASAddress(e->device, &addrInfo);
        e->blasHandles.push_back({ tasks[i].handle, tasks[i].storage, meshes[i]->blasAddress });
        delete tasks[i].buildInfo.pGeometries;
    }

    // --- PHASE 5: BUILD TLAS ---
    // Create instance buffer (now blasAddresses are valid)
    AllocatedBuffer instBuffer = create_buffer(e->allocator,
        instances.size() * sizeof(VkAccelerationStructureInstanceKHR),
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VMA_MEMORY_USAGE_CPU_TO_GPU, e);

    // Update instance BLAS references now that addresses are known
    for (size_t i = 0; i < meshes.size(); i++) {
        instances[i].accelerationStructureReference = meshes[i]->blasAddress;
    }

    void* mapped;
    vmaMapMemory(e->allocator, instBuffer.allocation, &mapped);
    memcpy(mapped, instances.data(), instances.size() * sizeof(VkAccelerationStructureInstanceKHR));
    vmaUnmapMemory(e->allocator, instBuffer.allocation);

    VkBufferDeviceAddressInfo instAddrInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = instBuffer.buffer
    };
    instBuffer.address = vkGetBufferDeviceAddress(e->device, &instAddrInfo);

    // Patch the real instance buffer address into tlasBuildInfo
    instancesData.data.deviceAddress = instBuffer.address;
    tlasGeo.geometry.instances = instancesData;
    tlasBuildInfo.pGeometries = &tlasGeo;

    // Create TLAS storage
    e->tlasStorage = create_buffer(e->allocator, tlasSizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY, e);

    VkAccelerationStructureCreateInfoKHR tlasCreateInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = e->tlasStorage.buffer,
        .size = tlasSizeInfo.accelerationStructureSize,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
    };
    e->pfn_vkCreateAS(e->device, &tlasCreateInfo, nullptr, &e->tlasHandle);

    immediate_submit([&](VkCommandBuffer cmd) {
        tlasBuildInfo.dstAccelerationStructure = e->tlasHandle;
        tlasBuildInfo.scratchData.deviceAddress = alignedScratchAddr;
        VkAccelerationStructureBuildRangeInfoKHR range{ instanceCount, 0, 0, 0 };
        const VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;
        e->pfn_vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tlasBuildInfo, &pRange);
        }, e);

    e->tlasInstanceBuffer = instBuffer;
    destroy_buffer(globalScratch, e);
}