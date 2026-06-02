#include <raytrace.h>
#include <engine.h>
#include <vulkan/vulkan_raii.hpp> 

void init_acceleration_structure(Engine* e, std::vector<std::shared_ptr<MeshAsset>>& meshes) {
    e->pfn_vkGetBuildSizes = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(e->device, "vkGetAccelerationStructureBuildSizesKHR");
    e->pfn_vkCreateAS = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(e->device, "vkCreateAccelerationStructureKHR");
    e->pfn_vkGetASAddress = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(e->device, "vkGetAccelerationStructureDeviceAddressKHR");
    e->pfn_vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(e->device, "vkCmdBuildAccelerationStructuresKHR");
    e->pfn_vkDestroyAS = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(e->device, "vkDestroyAccelerationStructureKHR");

    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
    VkPhysicalDeviceProperties2 props2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &asProps };
    vkGetPhysicalDeviceProperties2(e->physicalDevice, &props2);
    uint64_t scratchAlignment = asProps.minAccelerationStructureScratchOffsetAlignment;

    struct BLASBuildTask {
        VkAccelerationStructureBuildGeometryInfoKHR buildInfo;
        VkAccelerationStructureBuildRangeInfoKHR range;
        VkAccelerationStructureGeometryKHR geometry;
        VkAccelerationStructureGeometryTrianglesDataKHR triangles;
        AllocatedBuffer storage;
        VkAccelerationStructureKHR handle;
    };

    std::vector<BLASBuildTask> tasks;
    tasks.reserve(meshes.size());

    uint64_t maxScratchSize = 0;

    for (auto& mesh : meshes) {
        BLASBuildTask task{};
        uint32_t triangleCount = mesh->meshBuffers.indexCount / 3;

        task.triangles = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
            .vertexData = {.deviceAddress = mesh->meshBuffers.vertexBufferAddress },
            .vertexStride = sizeof(Vertex),
            .maxVertex = mesh->meshBuffers.indexCount,
            .indexType = VK_INDEX_TYPE_UINT32,
            .indexData = {.deviceAddress = mesh->meshBuffers.indexBufferAddress },
        };

        task.geometry = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
            .geometry = {.triangles = task.triangles },
            .flags = VK_GEOMETRY_OPAQUE_BIT_KHR
        };

        task.buildInfo = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
            .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .geometryCount = 1,
            .pGeometries = &task.geometry 
        };

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        e->pfn_vkGetBuildSizes(e->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &task.buildInfo, &triangleCount, &sizeInfo);

        maxScratchSize = std::max(maxScratchSize, sizeInfo.buildScratchSize);

        task.storage = create_buffer(e->allocator, sizeInfo.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY, e);

        VkAccelerationStructureCreateInfoKHR createInfo{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .buffer = task.storage.buffer,
            .size = sizeInfo.accelerationStructureSize,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
        };
        e->pfn_vkCreateAS(e->device, &createInfo, nullptr, &task.handle);

        task.buildInfo.dstAccelerationStructure = task.handle;
        task.range = { triangleCount, 0, 0, 0 };

        tasks.push_back(task);
    }

    AllocatedBuffer globalScratch = create_buffer(e->allocator, maxScratchSize + scratchAlignment,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY, e);

    VkBufferDeviceAddressInfo scratchAddrInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, globalScratch.buffer };
    VkDeviceAddress rawScratchAddr = vkGetBufferDeviceAddress(e->device, &scratchAddrInfo);
    VkDeviceAddress alignedScratchAddr = (rawScratchAddr + scratchAlignment - 1) & ~(VkDeviceAddress)(scratchAlignment - 1);

    immediate_submit([&](VkCommandBuffer cmd) {
        for (auto& task : tasks) {
            task.geometry.geometry.triangles = task.triangles;
            task.buildInfo.pGeometries = &task.geometry;
            task.buildInfo.scratchData.deviceAddress = alignedScratchAddr;

            const VkAccelerationStructureBuildRangeInfoKHR* pRange = &task.range;
            e->pfn_vkCmdBuildAccelerationStructuresKHR(cmd, 1, &task.buildInfo, &pRange);

            VkMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        }
        }, e);

    std::vector<VkAccelerationStructureInstanceKHR> instances;
    for (size_t i = 0; i < tasks.size(); i++) {
        VkAccelerationStructureDeviceAddressInfoKHR addrInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, nullptr, tasks[i].handle };
        meshes[i]->blasAddress = e->pfn_vkGetASAddress(e->device, &addrInfo);
        e->blasHandles.push_back({ tasks[i].handle, tasks[i].storage, meshes[i]->blasAddress });

        VkAccelerationStructureInstanceKHR inst{};
        glm::mat4 transposed = glm::transpose(meshes[i]->worldTransform);
        memcpy(&inst.transform, &transposed, sizeof(inst.transform));
        inst.instanceCustomIndex = i;
        inst.mask = 0xFF;
        inst.accelerationStructureReference = meshes[i]->blasAddress;
        inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instances.push_back(inst);
    }

    // (Standard TLAS build follows...)
    AllocatedBuffer instBuffer = create_buffer(e->allocator, instances.size() * sizeof(VkAccelerationStructureInstanceKHR),
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VMA_MEMORY_USAGE_CPU_TO_GPU, e);

    void* mapped;
    vmaMapMemory(e->allocator, instBuffer.allocation, &mapped);
    memcpy(mapped, instances.data(), instances.size() * sizeof(VkAccelerationStructureInstanceKHR));
    vmaUnmapMemory(e->allocator, instBuffer.allocation);

    VkBufferDeviceAddressInfo instAddrInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, instBuffer.buffer };
    instBuffer.address = vkGetBufferDeviceAddress(e->device, &instAddrInfo);

    VkAccelerationStructureGeometryInstancesDataKHR instancesData{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
    instancesData.data.deviceAddress = instBuffer.address;

    VkAccelerationStructureGeometryKHR tlasGeo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    tlasGeo.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tlasGeo.geometry.instances = instancesData;

    VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    tlasBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tlasBuildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    tlasBuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tlasBuildInfo.geometryCount = 1;
    tlasBuildInfo.pGeometries = &tlasGeo;

    uint32_t instanceCount = (uint32_t)instances.size();
    VkAccelerationStructureBuildSizesInfoKHR tlasSizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    e->pfn_vkGetBuildSizes(e->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlasBuildInfo, &instanceCount, &tlasSizeInfo);

    e->tlasStorage = create_buffer(e->allocator, tlasSizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY, e);

    VkAccelerationStructureCreateInfoKHR tlasCreateInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    tlasCreateInfo.buffer = e->tlasStorage.buffer;
    tlasCreateInfo.size = tlasSizeInfo.accelerationStructureSize;
    tlasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
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
    vkDeviceWaitIdle(e->device);
}

void rebuild_tlas(Engine* e)
{
    if (!e) return;

    if (e->tlasHandle != VK_NULL_HANDLE) {
        if (e->pfn_vkDestroyAS) {
            e->pfn_vkDestroyAS(e->device, e->tlasHandle, nullptr);
        }
        e->tlasHandle = VK_NULL_HANDLE;
    }

    if (e->tlasStorage.buffer != VK_NULL_HANDLE) {
        destroy_buffer(e->tlasStorage, e);
        e->tlasStorage = {};
    }

    if (e->tlasInstanceBuffer.buffer != VK_NULL_HANDLE) {
        destroy_buffer(e->tlasInstanceBuffer, e);
        e->tlasInstanceBuffer = {};
    } 

    for (auto& blas : e->blasHandles) {
        if (blas.handle != VK_NULL_HANDLE) {
            if (e->pfn_vkDestroyAS) {
                e->pfn_vkDestroyAS(e->device, blas.handle, nullptr);
            }
        }
        destroy_buffer(blas.buffer, e);
    }
    e->blasHandles.clear();

    init_acceleration_structure(e, e->testMeshes);

    DescriptorWriter writer;
    writer.write_tlas(4, e->tlasHandle);
    writer.update_set(e->device, e->bindlessSet);
}