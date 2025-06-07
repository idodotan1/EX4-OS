//
// Created by idodo on 04/06/2025.
//

#include "MemoryConstants.h"
#include "PhysicalMemory.h"
uint64_t get_index_at_level(uint64_t virtualAddress, int level)
{
    uint64_t mask = (1ULL << OFFSET_WIDTH) - 1;
    int shift = OFFSET_WIDTH * (TABLES_DEPTH - 1 - level);
    return (virtualAddress >> shift) & mask;
}

void DFS(uint64_t frame,
         int    depth,
         uint64_t curr_parent_frame,
         uint64_t curr_parent_index,
         uint64_t& best_empty_frame,
         uint64_t& best_parent_frame,
         uint64_t& best_parent_index,
         uint64_t& max_referenced_frame)
{
    bool empty_table = true;
    for (uint64_t i = 0; i < PAGE_SIZE; i++)
    {
        uint64_t addr = frame * PAGE_SIZE + i;
        word_t child_frame;
        PMread(addr, &child_frame);
        if (child_frame != 0)
        {
            empty_table = false;
            if (child_frame > max_referenced_frame)
            {
                max_referenced_frame = child_frame;
            }
            if (depth + 1 < TABLES_DEPTH)
            {
                DFS(child_frame,
                    depth + 1,             // next depth
                    frame,                 // this frame is the parent
                    i,                     // this slot is the index
                    best_empty_frame,
                    best_parent_frame,
                    best_parent_index,
                    max_referenced_frame);
            }
        }
    }
    if (empty_table && best_empty_frame == -1 && frame != 0)
    {
        best_empty_frame = frame;
        best_parent_frame   = curr_parent_frame;
        best_parent_index   = curr_parent_index;
    }
}

uint64_t distance(uint64_t p1, uint64_t p2)
{
    uint64_t TOTAL_PAGES = 1ULL << (VIRTUAL_ADDRESS_WIDTH - OFFSET_WIDTH);
    uint64_t diff = (p1 + TOTAL_PAGES - p2) % TOTAL_PAGES;
    if (diff < TOTAL_PAGES - diff) {
        return diff;
    }
    return TOTAL_PAGES - diff;
}

// add two out‐params: &victim_parent_frame and &victim_parent_index
void find_page_to_evict(uint64_t frame,
                        int     depth,
                        uint64_t page_num_prefix,
                        uint64_t& best_distance,
                        uint64_t& victim_frame,
                        uint64_t& victim_page_num,
                        uint64_t load_page_num,
                        bool&    first_leaf,
        // NEW:
                        uint64_t& victim_parent_frame,
                        uint64_t& victim_parent_index)
{
    for (int i = 0; i < PAGE_SIZE; i++)
    {
        word_t child;
        PMread(frame*PAGE_SIZE + i, &child);
        if (child != 0)
        {
            uint64_t next_prefix = (page_num_prefix << OFFSET_WIDTH) | i;
            if (depth + 1 < TABLES_DEPTH)
            {
                find_page_to_evict(child,
                                   depth+1,
                                   next_prefix,
                                   best_distance,
                                   victim_frame,
                                   victim_page_num,
                                   load_page_num,
                                   first_leaf,
                                   victim_parent_frame,
                                   victim_parent_index);
            }
            else
            {
                uint64_t dist = distance(next_prefix, load_page_num);
                if (first_leaf || dist > best_distance) {
                    best_distance           = dist;
                    victim_frame            = child;
                    victim_page_num         = next_prefix;
                    first_leaf              = false;
                    // record who pointed at it:
                    victim_parent_frame     = frame;
                    victim_parent_index     = i;
                }
            }
        }
    }
}


uint64_t evict_and_return_frame(uint64_t virtualAddress)
{
    uint64_t load_page_num        = virtualAddress >> OFFSET_WIDTH;
    uint64_t best_distance        = 0;
    bool     first_leaf           = true;
    uint64_t victim_frame         = (uint64_t)-1;
    uint64_t victim_page_num      = 0;
    // NEW helpers for parent info:
    uint64_t victim_parent_frame  = 0;
    uint64_t victim_parent_index  = 0;

    find_page_to_evict(0,
                       0,
                       0,
                       best_distance,
                       victim_frame,
                       victim_page_num,
                       load_page_num,
                       first_leaf,
                       victim_parent_frame,
                       victim_parent_index);

    // 1) Evict the leaf
    PMevict(victim_frame, victim_page_num);

    // 2) Unlink that exact pointer
    PMwrite(victim_parent_frame * PAGE_SIZE + victim_parent_index, 0);

    // 3) Done
    return victim_frame;
}


uint64_t get_new_frame(uint64_t virtualAddress, uint64_t parent_frame)
{
    uint64_t best_parent_frame = 0;
    uint64_t best_parent_index = 0;
    uint64_t best_empty_frame = -1;
    uint64_t max_referenced_frame = 0;

// initial call:
    DFS(0,          // frame = root
        0,          // depth = 0
        0,          // curr_parent_frame = unused for root
        0,          // curr_parent_index = unused for root
        best_empty_frame,
        best_parent_frame,
        best_parent_index,
        max_referenced_frame);
    if (best_empty_frame != -1 && best_empty_frame != parent_frame)
    {
        PMwrite(best_parent_frame * PAGE_SIZE + best_parent_index, 0);
        return best_empty_frame;
    }
    if (max_referenced_frame + 1 < NUM_FRAMES)
    {
        return max_referenced_frame + 1;
    }
    return evict_and_return_frame(virtualAddress);
}

uint64_t get_physical_address(uint64_t virtualAddress)
{
    uint64_t frame = 0;
    uint64_t page_number = virtualAddress >> OFFSET_WIDTH;
    for (int level = 0; level < TABLES_DEPTH; level++)
    {
        uint64_t idx = get_index_at_level(page_number, level);
        uint64_t cell_addr = frame * PAGE_SIZE + idx;
        word_t next_frame;
        PMread(cell_addr, &next_frame);
        if (next_frame == 0)
        {
            uint64_t new_frame = get_new_frame(virtualAddress,frame);
            PMwrite(cell_addr, new_frame);
            next_frame = new_frame;
            if (level + 1 < TABLES_DEPTH)
            {
                for (int k = 0; k < PAGE_SIZE; k++)
                {
                    PMwrite(new_frame * PAGE_SIZE + k, 0);
                }
            }
            else
            {
                uint64_t page_num = virtualAddress >> OFFSET_WIDTH;
                PMrestore(new_frame, page_num);
            }
        }
        frame = next_frame;
    }
    uint64_t offset = virtualAddress & ((1ULL << OFFSET_WIDTH) - 1);
    return frame * PAGE_SIZE + offset;
}

void VMinitialize()
{
    for (uint64_t i = 0; i < PAGE_SIZE; i++)
    {
        PMwrite(i, 0);
    }
}

int VMread(uint64_t virtualAddress, word_t* value)
{
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE)
    {
        return 0;
    }
    uint64_t phys_addr = get_physical_address(virtualAddress);
    PMread(phys_addr, value);
    return 1;
}

int VMwrite(uint64_t virtualAddress, word_t value)
{
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE)
    {
        return 0;
    }
    uint64_t phys_addr = get_physical_address(virtualAddress);
    PMwrite(phys_addr, value);
    return 1;
}