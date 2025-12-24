#ifndef MEMORY_ALLOCATION_H
#define MEMORY_ALLOCATION_H

#include <stddef.h>
#include <stdbool.h>

typedef enum {
    MCKUSICK_KARELS,  
    POWER_OF_2         
} AllocationAlgorithm;

#define MK_NUM_SIZE_CLASSES 32
typedef struct {
    void** free_lists;    
    size_t* class_sizes;  
    void* memory_pool;
    size_t total_size;
    size_t used_size;
    size_t num_classes;
} McKusickKarelsAllocator;

#define MAX_ORDER 20
typedef struct BuddyBlock {
    size_t order;       
    bool is_free;
    struct BuddyBlock* next;
} BuddyBlock;

typedef struct {
    BuddyBlock** free_lists; 
    void* memory_pool;
    size_t total_size;
    size_t used_size;
    size_t max_order;
} PowerOf2Allocator;

typedef struct {
    AllocationAlgorithm type;
    void* allocator;
} MemoryAllocator;

MemoryAllocator* create_allocator(AllocationAlgorithm type, size_t total_size);
void destroy_allocator(MemoryAllocator* allocator);
void* allocate_memory(MemoryAllocator* allocator, size_t size);
void free_memory(MemoryAllocator* allocator, void* ptr, size_t size);
void print_memory_status(MemoryAllocator* allocator);

typedef struct {
    double avg_allocation_time;
    double avg_deallocation_time;
    size_t internal_fragmentation;
    size_t external_fragmentation;
    size_t failed_allocations;
    double total_time;
    double memory_efficiency;
} BenchmarkResult;

BenchmarkResult benchmark_algorithm(AllocationAlgorithm algorithm, size_t pool_size,
                                     size_t* allocation_sizes, size_t num_allocations);
void print_benchmark_results(const char* algorithm_name, BenchmarkResult result);
void compare_algorithms(size_t pool_size, size_t* allocation_sizes, size_t num_allocations);

#endif 
