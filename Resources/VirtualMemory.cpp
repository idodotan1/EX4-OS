////
// Created by idodo on 04/06/2025.
//

#include "PhysicalMemory.h"

uint64_t get_index_at_level(uint64_t virtualAddress, int level)
{
    uint64_t mask = (1ULL << OFFSET_WIDTH) - 1;
    int shift = OFFSET_WIDTH * (TABLES_DEPTH - 1 - level);
    return (virtualAddress >> shift) & mask;
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

uint64_t DFS(uint64_t called_page,uint64_t prev_frame,uint64_t frame,
             int    depth,uint64_t curr_parent_frame,uint64_t curr_path,
             uint64_t& best_parent_frame,uint64_t& best_parent_index,
             uint64_t& max_referenced_frame,uint64_t& max_distance,
             uint64_t& max_distance_page)
{
    if (frame > max_referenced_frame){
        max_referenced_frame = frame;
    }
    if (depth == TABLES_DEPTH) {
        uint64_t dist = distance(called_page, curr_path);
        if (dist > max_distance) {
            max_distance = dist;
            max_distance_page = curr_path;
        }
        return -1;
    }
    bool empty_table = true;
    uint64_t res = -1;
    for (uint64_t i = 0; i < PAGE_SIZE; i++)
    {
        uint64_t addr = frame * PAGE_SIZE + i;
        word_t child_frame;
        PMread(addr, &child_frame);
        if (child_frame != 0)
        {
            empty_table = false;
            uint64_t next_path = curr_path*PAGE_SIZE+i;
            uint64_t temp=DFS(called_page,prev_frame,child_frame,
                              depth + 1,frame,
                              next_path,best_parent_frame,
                              best_parent_index,max_referenced_frame,
                              max_distance,max_distance_page);
            if(temp !=(uint64_t)-1)
            {
                res = temp;
            }
        }
    }
    if (empty_table && frame != prev_frame)
    {
        best_parent_frame   = curr_parent_frame;
        best_parent_index   = curr_path%PAGE_SIZE;
        return frame;
    }
    return res;
}


uint64_t evict_and_return_frame(uint64_t page_to_evict)
{
    const uint64_t mask = PAGE_SIZE - 1;
    uint64_t offset = page_to_evict & mask;
    word_t curr_frame   = 0;
    uint64_t parent_slot  = 0;

    for (int lvl = 0; lvl < TABLES_DEPTH; lvl++) {
        uint64_t slot = get_index_at_level(page_to_evict, lvl);
        parent_slot = curr_frame;
        PMread(curr_frame*PAGE_SIZE + slot, &curr_frame);
    }
    PMevict(curr_frame, page_to_evict);
    PMwrite(parent_slot*PAGE_SIZE + offset, 0);
    return curr_frame;
}


uint64_t get_new_frame(uint64_t virtualAddress, uint64_t parent_frame)
{
    uint64_t best_parent_frame = -1;
    uint64_t best_parent_index = -1;
    uint64_t max_referenced_frame = 0;
    uint64_t max_distance = 0;
    uint64_t max_distance_page = -1;
    uint64_t called_page = virtualAddress >> OFFSET_WIDTH;
    uint64_t returned_frame = DFS(called_page,parent_frame,0,
                                  0,0,0,
                                  best_parent_frame,best_parent_index,
                                  max_referenced_frame,max_distance,
                                  max_distance_page);
    if (returned_frame !=(uint64_t) -1 )
    {
        PMwrite(best_parent_frame * PAGE_SIZE + best_parent_index, 0);
        return returned_frame;
    }
    if (max_referenced_frame + 1 < NUM_FRAMES)
    {
        return max_referenced_frame + 1;
    }
    return evict_and_return_frame(max_distance_page);
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
