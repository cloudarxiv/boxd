/*******************************************************************************
    Copyright (c) 2016 NVIDIA Corporation

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
    deal in the Software without restriction, including without limitation the
    rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

        The above copyright notice and this permission notice shall be
        included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.

*******************************************************************************/

#include "uvm_test.h"
#include "uvm_perf_events.h"
#include "uvm_va_space.h"

// Entry of the event callback list
typedef struct
{
    uvm_perf_event_callback_t callback;

    struct list_head callback_list_node;
} callback_desc_t;

// Cache for callback descriptor list entries
static struct kmem_cache *g_callback_desc_cache;

// Check if the callback list already contains an entry for the given callback. Caller needs to hold (at least) read
// va_space_events lock
static callback_desc_t *event_list_find_callback(uvm_perf_va_space_events_t *va_space_events,
                                                 struct list_head *callback_list, uvm_perf_event_callback_t callback)
{
    callback_desc_t *callback_desc;

    uvm_assert_rwsem_locked(&va_space_events->lock);

    list_for_each_entry(callback_desc, callback_list, callback_list_node) {
        if (callback_desc->callback == callback)
            return callback_desc;
    }

    return NULL;
}

NV_STATUS uvm_perf_register_event_callback_locked(uvm_perf_va_space_events_t *va_space_events,
                                                  uvm_perf_event_t event_id,
                                                  uvm_perf_event_callback_t callback)
{
    callback_desc_t *callback_desc;
    struct list_head *callback_list;

    UVM_ASSERT(event_id >= 0 && event_id < UVM_PERF_EVENT_COUNT);
    UVM_ASSERT(callback);

    uvm_assert_rwsem_locked_write(&va_space_events->lock);

    callback_list = &va_space_events->event_callbacks[event_id];

    UVM_ASSERT(!event_list_find_callback(va_space_events, callback_list, callback));

    callback_desc = kmem_cache_alloc(g_callback_desc_cache, NV_UVM_GFP_FLAGS);
    if (!callback_desc)
        return NV_ERR_NO_MEMORY;

    callback_desc->callback = callback;
    list_add_tail(&callback_desc->callback_list_node, callback_list);

    return NV_OK;
}

NV_STATUS uvm_perf_register_event_callback(uvm_perf_va_space_events_t *va_space_events, uvm_perf_event_t event_id,
                                           uvm_perf_event_callback_t callback)
{
    NV_STATUS status;

    uvm_down_write(&va_space_events->lock);
    status = uvm_perf_register_event_callback_locked(va_space_events, event_id, callback);
    uvm_up_write(&va_space_events->lock);

    return status;
}

void uvm_perf_unregister_event_callback_locked(uvm_perf_va_space_events_t *va_space_events, uvm_perf_event_t event_id,
                                               uvm_perf_event_callback_t callback)
{
    callback_desc_t *callback_desc;
    struct list_head *callback_list;

    UVM_ASSERT(event_id >= 0 && event_id < UVM_PERF_EVENT_COUNT);
    UVM_ASSERT(callback);

    uvm_assert_rwsem_locked_write(&va_space_events->lock);

    callback_list = &va_space_events->event_callbacks[event_id];
    callback_desc = event_list_find_callback(va_space_events, callback_list, callback);

    if (!callback_desc)
        return;

    list_del(&callback_desc->callback_list_node);

    kmem_cache_free(g_callback_desc_cache, callback_desc);
}

void uvm_perf_unregister_event_callback(uvm_perf_va_space_events_t *va_space_events, uvm_perf_event_t event_id,
                                        uvm_perf_event_callback_t callback)
{
    uvm_down_write(&va_space_events->lock);
    uvm_perf_unregister_event_callback_locked(va_space_events, event_id, callback);
    uvm_up_write(&va_space_events->lock);
}

void uvm_perf_event_notify(uvm_perf_va_space_events_t *va_space_events, uvm_perf_event_t event_id,
                           uvm_perf_event_data_t *event_data)
{
    callback_desc_t *callback_desc;
    struct list_head *callback_list;

    UVM_ASSERT(event_id >= 0 && event_id < UVM_PERF_EVENT_COUNT);
    UVM_ASSERT(event_data);

    callback_list = &va_space_events->event_callbacks[event_id];

    uvm_down_read(&va_space_events->lock);
    // pr_info("migration happen 1.2.....\n");
    // Invoke all registered callbacks for the events
    list_for_each_entry(callback_desc, callback_list, callback_list_node) {
        
        callback_desc->callback(event_id, event_data);
    }

    uvm_up_read(&va_space_events->lock);
}
/*#ifdef DRIVER_STATE
void uvm_perf_event_update(uvm_perf_va_space_events_t *va_space_events, uvm_perf_event_t event_id,
                           uvm_perf_event_data_t *event_data)
{
    
    uvm_va_space_t *va_space = va_space_events->va_space;
    uvm_processor_id_t dest_id = event_data->residency_update.proc_id;
    if(event_id==UVM_PREF_EVENT_UPDATE_RESIDENCY){
        uvm_assert_rwsem_locked_write(&va_space->lock);
        if(UVM_ID_IS_CPU(dest_id))
        va_space->permanent_counters[dest_id.val][UvmCounterNameCPUResident]+=page_count;
        else
        va_space->permanent_counters[dest_id.val][UvmCounterNameGpuResident]+=page_count;
    }
}

#endif*/

bool uvm_perf_is_event_callback_registered(uvm_perf_va_space_events_t *va_space_events,
                                           uvm_perf_event_t event_id,
                                           uvm_perf_event_callback_t callback)
{
    callback_desc_t *callback_desc;
    struct list_head *callback_list;

    uvm_assert_rwsem_locked(&va_space_events->lock);

    callback_list = &va_space_events->event_callbacks[event_id];
    callback_desc = event_list_find_callback(va_space_events, callback_list, callback);

    return callback_desc != NULL;
}

NV_STATUS uvm_perf_init_va_space_events(uvm_va_space_t *va_space, uvm_perf_va_space_events_t *va_space_events)
{
    unsigned event_id;

    uvm_init_rwsem(&va_space_events->lock, UVM_LOCK_ORDER_VA_SPACE_EVENTS);

    // Initialize event callback lists
    for (event_id = 0; event_id < UVM_PERF_EVENT_COUNT; ++event_id)
        INIT_LIST_HEAD(&va_space_events->event_callbacks[event_id]);

    va_space_events->va_space = va_space;

    return NV_OK;
}

void uvm_perf_destroy_va_space_events(uvm_perf_va_space_events_t *va_space_events)
{
    unsigned event_id;

    // If the va_space member was not set, va_space creation failed before initializing its va_space_events member. We
    // are done.
    if (!va_space_events->va_space)
        return;

    // Destroy all event callback lists' entries
    for (event_id = 0; event_id < UVM_PERF_EVENT_COUNT; ++event_id) {
        callback_desc_t *callback_desc, *callback_desc_tmp;
        struct list_head *callback_list;

        callback_list = &va_space_events->event_callbacks[event_id];

        list_for_each_entry_safe(callback_desc, callback_desc_tmp, callback_list, callback_list_node) {
            list_del(&callback_desc->callback_list_node);
            kmem_cache_free(g_callback_desc_cache, callback_desc);
        }
    }

    va_space_events->va_space = NULL;
}

NV_STATUS uvm_perf_events_init(void)
{
    g_callback_desc_cache = NV_KMEM_CACHE_CREATE("uvm_perf_callback_list", callback_desc_t);
    if (!g_callback_desc_cache)
        return NV_ERR_NO_MEMORY;

    return NV_OK;
}

void uvm_perf_events_exit(void)
{
    kmem_cache_destroy_safe(&g_callback_desc_cache);
}

void uvm_perf_event_notify_cpu_fault(uvm_perf_va_space_events_t *va_space_events,
                                                   uvm_va_block_t *va_block,
                                                   uvm_processor_id_t preferred_location,
                                                   NvU64 fault_va,
                                                   bool is_write,
                                                   NvU32 cpu_num,
                                                   NvU64 pc)
{
    uvm_perf_event_data_t event_data =
        {
            .fault =
                {
                    .space         = va_space_events->va_space,
                    .block         = va_block,
                    .proc_id       = UVM_ID_CPU,
                    .preferred_location = preferred_location,
                }
        };

    event_data.fault.cpu.fault_va = fault_va;
    event_data.fault.cpu.is_write = is_write;
    event_data.fault.cpu.pc       = pc;
    event_data.fault.cpu.cpu_num  = cpu_num;

        uvm_assert_rwsem_locked_write(&va_space_events->va_space->lock);
         va_space_events->va_space->permanent_counters[0][UvmCounterNameCpuPageFaultCount]++;
        //uvm_up_write(&va_space->lock);

    uvm_perf_event_notify(va_space_events, UVM_PERF_EVENT_FAULT, &event_data);
}

void uvm_perf_event_notify_migration(uvm_perf_va_space_events_t *va_space_events,
                                                   uvm_push_t *push,
                                                   uvm_va_block_t *va_block,
                                                   uvm_processor_id_t dst,
                                                   uvm_processor_id_t src,
                                                   NvU64 address,
                                                   NvU64 bytes,
                                                   uvm_va_block_transfer_mode_t transfer_mode,
                                                   uvm_make_resident_cause_t cause,
                                                   uvm_make_resident_context_t *make_resident_context)
{ //  pr_info("migration happen.....\n");
   
    uvm_perf_event_data_t event_data =
        {
            .migration =
                {
                    .push                  = push,
                    .block                 = va_block,
                    .dst                   = dst,
                    .src                   = src,
                    .address               = address,
                    .bytes                 = bytes,
                    .transfer_mode         = transfer_mode,
                    .cause                 = cause,
                    .make_resident_context = make_resident_context,
                }
        };

        uvm_assert_rwsem_locked_write(&va_space_events->va_space->lock);
        if(cause==UVM_MAKE_RESIDENT_CAUSE_EVICTION){
         unsigned long int pages=bytes/UVM_PAGE_SIZE_4K;
         va_space_events->va_space->permanent_counters[src.val][UvmCounterNameGpuEvictions]+=pages;}

        if(UVM_ID_IS_CPU(dst) && UVM_ID_IS_CPU(src)){
            ;
        }

        else{
            unsigned long int pages=bytes/UVM_PAGE_SIZE_4K;
            if(UVM_ID_IS_CPU(dst)){
                 va_space_events->va_space->permanent_counters[src.val][UvmCounterNameBytesXferDtH]+=bytes;
                 va_space_events->va_space->permanent_counters[src.val][UvmCounterNameGpuResident]-=pages;
                 va_space_events->va_space->permanent_counters[dst.val][UvmCounterNameCPUResident]+=pages;
            }
            if(UVM_ID_IS_CPU(src)){
                 va_space_events->va_space->permanent_counters[dst.val][UvmCounterNameBytesXferHtD]+=bytes;
                 va_space_events->va_space->permanent_counters[dst.val][UvmCounterNameGpuResident]+=pages;
                 va_space_events->va_space->permanent_counters[src.val][UvmCounterNameCPUResident]-=pages;
            }
        }



    uvm_perf_event_notify(va_space_events, UVM_PERF_EVENT_MIGRATION, &event_data);
}

void uvm_perf_event_notify_gpu_fault(uvm_perf_va_space_events_t *va_space_events,
                                                   uvm_va_block_t *va_block,
                                                   uvm_gpu_id_t gpu_id,
                                                   uvm_processor_id_t preferred_location,
                                                   uvm_fault_buffer_entry_t *buffer_entry,
                                                   NvU32 batch_id,
                                                   bool is_duplicate)
{
    uvm_perf_event_data_t event_data =
        {
            .fault =
                {
                    .space            = va_space_events->va_space,
                    .block            = va_block,
                    .proc_id          = gpu_id,
                    .preferred_location = preferred_location,
                },
        };

    event_data.fault.gpu.buffer_entry = buffer_entry;
    event_data.fault.gpu.batch_id     = batch_id;
    event_data.fault.gpu.is_duplicate = is_duplicate;

    // pr_info("fault type: %d\n",buffer_entry->is_replayable);
        uvm_assert_rwsem_locked_write(&va_space_events->va_space->lock);
         va_space_events->va_space->permanent_counters[gpu_id.val][UvmCounterNameGpuPageFaultCount]++;


    uvm_perf_event_notify(va_space_events, UVM_PERF_EVENT_FAULT, &event_data);
}

void uvm_perf_event_notify_gpu_residency_update(uvm_perf_va_space_events_t *va_space_events,
                                                   uvm_va_block_t *va_block,
                                                   uvm_processor_id_t dest_id,
                                                   uvm_processor_id_t src_id,
                                                   NvU32 page_count)
{   //uvm_va_space_t *va_space = va_space_events->va_space;
    uvm_perf_event_data_t event_data =
        {
            .residency_update =
                {
                   
                    .block            = va_block,
                    .proc_id          = dest_id,
                    .page_count       = page_count,
                    .src_id           = src_id
                },
        };

    // pr_info("residancy update......: %d\n",page_count);
         uvm_assert_rwsem_locked_write(&va_space_events->va_space->lock);
        if(UVM_ID_IS_CPU(dest_id))
         va_space_events->va_space->permanent_counters[dest_id.val][UvmCounterNameCPUResident]+=page_count;
        else
         va_space_events->va_space->permanent_counters[dest_id.val][UvmCounterNameGpuResident]+=page_count;

    uvm_perf_event_notify(va_space_events, UVM_PREF_EVENT_UPDATE_RESIDENCY, &event_data);
}


void uvm_perf_event_notify_gpu_memory_update(uvm_perf_va_space_events_t *va_space_events,
                                                   uvm_processor_id_t dest_id,
                                                   NvU64 chunk_size,
                                                   bool is_added,
                                                   uvm_va_block_t* block)
{   //uvm_va_space_t *va_space = va_space_events->va_space;
    uvm_perf_event_data_t event_data =
        {
            .memeory_allocation =
                {
                   
                    .proc_id          = dest_id,
                    .chunk_size       = chunk_size,
                    .is_added         = is_added,
                    .block            = block
                },
        };

    // pr_info("chunk update......: %lu\n",chunk_size);
         uvm_assert_rwsem_locked_write(&va_space_events->va_space->lock);
         if(is_added)
         va_space_events->va_space->permanent_counters[dest_id.val][UvmCounterNameGpuMemory]+=chunk_size;
         else
         va_space_events->va_space->permanent_counters[dest_id.val][UvmCounterNameGpuMemory]-=chunk_size;

    uvm_perf_event_notify(va_space_events, UVM_PREF_EVENT_GPU_MEMORY_ALLOCATION_CHANGED, &event_data);
}


void uvm_perf_event_notify_other_process_evicted(uvm_perf_va_space_events_t *va_space_events,
                                                   uvm_processor_id_t dest_id,
                                                   NvU64 chunk_size
                                                   )
{   //uvm_va_space_t *va_space = va_space_events->va_space;
    uvm_perf_event_data_t event_data =
        {
            .other_process_eviction =
                {
                   
                    .proc_id          = dest_id,
                    .chunk_size       = chunk_size,
                    .va_space         = va_space_events->va_space
                },
        };

    // pr_info("chunk update......: %lu\n",chunk_size);
         uvm_assert_rwsem_locked_write(&va_space_events->va_space->lock);
         va_space_events->va_space->permanent_counters[dest_id.val][UvmCounterNameOtherProcess]+=chunk_size;

    uvm_perf_event_notify(va_space_events, UVM_PREF_GPU_MEMEORY_OTHER_PROCESS_MEMORY_EVICTED, &event_data);
}

