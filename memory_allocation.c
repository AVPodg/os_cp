#include "memory_allocation.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

static size_t next_power_of_2(size_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    #if SIZE_MAX > 0xFFFFFFFF // Если size_t — 64-битный, расширяем операцию
    n |= n >> 32;
    #endif
    return n + 1;
}

static size_t log2_size(size_t n) { 
    size_t result = 0;
    while (n > 1) {
        n >>= 1; // делит число на 2
        result++;
    }
    return result;
}

static McKusickKarelsAllocator* create_mk_allocator(size_t total_size) {
    McKusickKarelsAllocator* mk = (McKusickKarelsAllocator*)malloc(sizeof(McKusickKarelsAllocator));
    if (!mk) return NULL; // Проверка на нехватку памяти

    mk->memory_pool = malloc(total_size);
    if (!mk->memory_pool) {
        free(mk);
        return NULL;
    }

    mk->total_size = total_size;
    mk->used_size = 0;
    mk->num_classes = MK_NUM_SIZE_CLASSES; // MK_NUM_SIZE_CLASSES — константа из .h (храним кол-во классов размеров)

    mk->free_lists = (void**)calloc(mk->num_classes, sizeof(void*)); // массив списков свободных блоков
    mk->class_sizes = (size_t*)malloc(mk->num_classes * sizeof(size_t)); // реальный размер каждого блока
    
    if (!mk->free_lists || !mk->class_sizes) {
        free(mk->memory_pool);
        free(mk->free_lists);
        free(mk->class_sizes);
        free(mk);
        return NULL;
    }

    // 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, etc.
    size_t max_size_class = total_size / 2;
    for (size_t i = 0; i < mk->num_classes; i++) {

        if (i >= 60) { // исключаем переполнение
            mk->num_classes = i;
            break;
        }
        size_t size_class = 16ULL << i;  // 16 * 2^i
        if (size_class > max_size_class) {
            mk->num_classes = i;
            break;
        }
        mk->class_sizes[i] = size_class;
    }

    char* pool = (char*)mk->memory_pool; // для удобного передвижения по памяти
    size_t offset = 0;

    for (size_t class_idx = 0; class_idx < mk->num_classes && offset < total_size; class_idx++) {
        size_t block_size = mk->class_sizes[class_idx];
        
        if (block_size < sizeof(void*)) {
            continue;
        }
        
        size_t num_blocks = (total_size / 10) / block_size;
        
        if (num_blocks == 0) num_blocks = 1;
        if (num_blocks > 100) num_blocks = 100; // чтобы не забить весь пул одним классом

        for (size_t i = 0; i < num_blocks && offset + block_size <= total_size; i++) {
            // Выравнивание по границе указателя
            size_t alignment = sizeof(void*);  // Обычно 4 или 8 байт
            size_t aligned_offset = (offset + alignment - 1) & ~(alignment - 1);
            // Эта формула округляет offset вверх до ближайшего кратного alignment
            
            if (aligned_offset + block_size > total_size) {
                break;  // Не хватает места
            }
            
            // Получаем указатель на блок
            void** block = (void**)(pool + aligned_offset);
            
            // Добавляем блок в начало списка
            // Первые sizeof(void*) байт блока хранят указатель на следующий свободный блок
            *block = mk->free_lists[class_idx];  // Ставим текущую голову списка как следующий
            mk->free_lists[class_idx] = block;   // Делаем этот блок новой головой
            
            offset = aligned_offset + block_size;  // Сдвигаем offset
        }
    }

    return mk;
}

static void* mk_allocate(McKusickKarelsAllocator* mk, size_t size) {
    if (!mk || size == 0) return NULL;

    size_t class_idx = 0;
    while (class_idx < mk->num_classes && mk->class_sizes[class_idx] < size) {
        class_idx++;
    }

    if (class_idx >= mk->num_classes) {
        return NULL; 
    }

    // Проверяем есть ли свободные блоки в этом классе
    if (mk->free_lists[class_idx]) {
        void** block = (void**)mk->free_lists[class_idx];
        mk->free_lists[class_idx] = *block;
        mk->used_size += mk->class_sizes[class_idx];
        return block;
    }

    return NULL;
}

static void mk_free(McKusickKarelsAllocator* mk, void* ptr, size_t size) {
    if (!mk || !ptr) return;

    size_t class_idx = 0;
    while (class_idx < mk->num_classes && mk->class_sizes[class_idx] < size) {
        class_idx++;
    }

    if (class_idx >= mk->num_classes) return;

    void** block = (void**)ptr;
    *block = mk->free_lists[class_idx];
    mk->free_lists[class_idx] = block;
    mk->used_size -= mk->class_sizes[class_idx];
}

static void destroy_mk_allocator(McKusickKarelsAllocator* mk) {
    if (!mk) return;
    free(mk->memory_pool);
    free(mk->free_lists);
    free(mk->class_sizes);
    free(mk);
}

static PowerOf2Allocator* create_power_of_2_allocator(size_t total_size) {
    PowerOf2Allocator* p2 = (PowerOf2Allocator*)malloc(sizeof(PowerOf2Allocator));
    if (!p2) return NULL;

    size_t rounded_size = next_power_of_2(total_size); // размер всего пула
    
    p2->memory_pool = malloc(rounded_size);
    if (!p2->memory_pool) {
        free(p2);
        return NULL;
    }

    p2->total_size = rounded_size;
    p2->used_size = 0;
    p2->max_order = log2_size(rounded_size);

    p2->free_lists = (BuddyBlock**)calloc(p2->max_order + 1, sizeof(BuddyBlock*));
    if (!p2->free_lists) {
        free(p2->memory_pool);
        free(p2);
        return NULL;
    }

    BuddyBlock* initial_block = (BuddyBlock*)p2->memory_pool;
    initial_block->order = p2->max_order;
    initial_block->is_free = true;
    initial_block->next = NULL;
    p2->free_lists[p2->max_order] = initial_block;

    return p2;
}

static void* p2_allocate(PowerOf2Allocator* p2, size_t size) {
    if (!p2 || size == 0) return NULL;

    size_t total_size = size + sizeof(BuddyBlock);
    
    size_t required_size = next_power_of_2(total_size);
    size_t order = log2_size(required_size);

    if (order > p2->max_order) return NULL;

    size_t current_order = order;
    while (current_order <= p2->max_order && !p2->free_lists[current_order]) {
        current_order++;
    }

    if (current_order > p2->max_order) return NULL;

    while (current_order > order) {
        BuddyBlock* block = p2->free_lists[current_order];
        p2->free_lists[current_order] = block->next;

        current_order--;
        size_t buddy_size = 1 << current_order;

        BuddyBlock* buddy1 = block;
        BuddyBlock* buddy2 = (BuddyBlock*)((char*)block + buddy_size);

        buddy1->order = current_order;
        buddy1->is_free = true;
        buddy2->order = current_order;
        buddy2->is_free = true;

        buddy1->next = buddy2;
        buddy2->next = p2->free_lists[current_order];
        p2->free_lists[current_order] = buddy1;
    }

    BuddyBlock* block = p2->free_lists[order];
    p2->free_lists[order] = block->next;
    block->is_free = false;
    p2->used_size += (1 << order);

    return (void*)((char*)block + sizeof(BuddyBlock));
}

static void p2_free(PowerOf2Allocator* p2, void* ptr, size_t size) {
    if (!p2 || !ptr) return;
    (void)size; 

    if (ptr < (void*)((char*)p2->memory_pool + sizeof(BuddyBlock)) ||
        ptr >= (void*)((char*)p2->memory_pool + p2->total_size)) {
        return; 
    }

    BuddyBlock* block = (BuddyBlock*)((char*)ptr - sizeof(BuddyBlock));
    
    if ((void*)block < p2->memory_pool ||
        (void*)block >= (void*)((char*)p2->memory_pool + p2->total_size)) {
        return; 
    }
    
    if (block->is_free) return;

    size_t order = block->order;
    block->is_free = true;
    p2->used_size -= (1 << order);

     // 5. Пытаемся объединить с "другом" (buddy)
    while (order < p2->max_order) {
        size_t block_size = 1 << order;  // Размер текущего блока
        size_t block_offset = (char*)block - (char*)p2->memory_pool;
        
        // Вычисляем offset "друга" (XOR с размером блока)
        size_t buddy_offset = block_offset ^ block_size;
        
        if (buddy_offset >= p2->total_size) {
            break; // "Друг" за пределами пула
        }
        
        BuddyBlock* buddy = (BuddyBlock*)((char*)p2->memory_pool + buddy_offset);

        // Проверяем можно ли объединить:
        // 1) Друг должен быть свободен
        // 2) Должен быть того же порядка
        if (!buddy->is_free || buddy->order != order) {
            break; // Нельзя объединить
        }

        // Удаляем друга из списка свободных
        BuddyBlock** list = &p2->free_lists[order];
        while (*list && *list != buddy) {
            list = &(*list)->next;
        }
        if (*list == buddy) {
            *list = buddy->next;
        }

        // Объединяем блоки (выбираем тот, который начинается раньше)
        if (buddy < block) {
            block = buddy;
        }
        block->order = order + 1; // Увеличиваем порядок (удваиваем размер)
        order++;
    }

    // 6. Добавляем (возможно объединенный) блок в список свободных
    block->next = p2->free_lists[order];
    p2->free_lists[order] = block;
}

static void destroy_power_of_2_allocator(PowerOf2Allocator* p2) {
    if (!p2) return;
    free(p2->memory_pool);
    free(p2->free_lists);
    free(p2);
}

MemoryAllocator* create_allocator(AllocationAlgorithm type, size_t total_size) {
    MemoryAllocator* allocator = (MemoryAllocator*)malloc(sizeof(MemoryAllocator));
    if (!allocator) return NULL;

    allocator->type = type;

    if (type == MCKUSICK_KARELS) {
        allocator->allocator = create_mk_allocator(total_size);
    } else if (type == POWER_OF_2) {
        allocator->allocator = create_power_of_2_allocator(total_size);
    } else {
        free(allocator);
        return NULL;
    }

    if (!allocator->allocator) {
        free(allocator);
        return NULL;
    }

    return allocator;
}

void destroy_allocator(MemoryAllocator* allocator) {
    if (!allocator) return;

    if (allocator->type == MCKUSICK_KARELS) {
        destroy_mk_allocator((McKusickKarelsAllocator*)allocator->allocator);
    } else if (allocator->type == POWER_OF_2) {
        destroy_power_of_2_allocator((PowerOf2Allocator*)allocator->allocator);
    }

    free(allocator);
}

void* allocate_memory(MemoryAllocator* allocator, size_t size) {
    if (!allocator) return NULL;

    if (allocator->type == MCKUSICK_KARELS) {
        return mk_allocate((McKusickKarelsAllocator*)allocator->allocator, size);
    } else if (allocator->type == POWER_OF_2) {
        return p2_allocate((PowerOf2Allocator*)allocator->allocator, size);
    }

    return NULL;
}

void free_memory(MemoryAllocator* allocator, void* ptr, size_t size) {
    if (!allocator || !ptr) return;

    if (allocator->type == MCKUSICK_KARELS) {
        mk_free((McKusickKarelsAllocator*)allocator->allocator, ptr, size);
    } else if (allocator->type == POWER_OF_2) {
        p2_free((PowerOf2Allocator*)allocator->allocator, ptr, size);
    }
}

void print_memory_status(MemoryAllocator* allocator) {
    if (!allocator) return;

    printf("\n=== Memory Status ===\n");

    if (allocator->type == MCKUSICK_KARELS) {
        McKusickKarelsAllocator* mk = (McKusickKarelsAllocator*)allocator->allocator;
        printf("Algorithm: McKusick-Karels\n");
        printf("Total Size: %zu bytes\n", mk->total_size);
        printf("Used Size: %zu bytes\n", mk->used_size);
        printf("Free Size: %zu bytes\n", mk->total_size - mk->used_size);
        printf("Utilization: %.2f%%\n", (double)mk->used_size / mk->total_size * 100);
        printf("Number of Size Classes: %zu\n", mk->num_classes);
    } else if (allocator->type == POWER_OF_2) {
        PowerOf2Allocator* p2 = (PowerOf2Allocator*)allocator->allocator;
        printf("Algorithm: Power-of-2 (Buddy System)\n");
        printf("Total Size: %zu bytes\n", p2->total_size);
        printf("Used Size: %zu bytes\n", p2->used_size);
        printf("Free Size: %zu bytes\n", p2->total_size - p2->used_size);
        printf("Utilization: %.2f%%\n", (double)p2->used_size / p2->total_size * 100);
        printf("Max Order: %zu\n", p2->max_order);
    }

    printf("====================\n\n");
}

BenchmarkResult benchmark_algorithm(AllocationAlgorithm algorithm, size_t pool_size,
                                     size_t* allocation_sizes, size_t num_allocations) {
    BenchmarkResult result = {0};  // Обнуляем структуру
    // Создаем аллокатор
    MemoryAllocator* allocator = create_allocator(algorithm, pool_size);
    if (!allocator) return result;  // Если не удалось создать

    // Выделяем массивы для хранения указателей и размеров
    void** allocated_ptrs = (void**)malloc(num_allocations * sizeof(void*));
    size_t* actual_sizes = (size_t*)malloc(num_allocations * sizeof(size_t));
    
    // Проверяем выделились ли массивы
    if (!allocated_ptrs || !actual_sizes) {
        destroy_allocator(allocator);
        free(allocated_ptrs);
        free(actual_sizes);
        return result;
    }

    // Замеряем общее время начала
    clock_t start_time = clock();
    double total_alloc_time = 0;
    size_t successful_allocations = 0;
    size_t total_requested = 0;
    size_t total_allocated = 0;

    // Фаза выделения памяти
    for (size_t i = 0; i < num_allocations; i++) {
        // Замер времени для одного выделения
        clock_t alloc_start = clock();
        allocated_ptrs[i] = allocate_memory(allocator, allocation_sizes[i]);
        clock_t alloc_end = clock();

        // Добавляем к общему времени
        total_alloc_time += (double)(alloc_end - alloc_start) / CLOCKS_PER_SEC;

        if (allocated_ptrs[i]) {  // Если выделение удалось
            successful_allocations++;
            actual_sizes[i] = allocation_sizes[i];
            total_requested += allocation_sizes[i];
            
            // Вычисляем реально выделенный размер
            if (algorithm == MCKUSICK_KARELS) {
                // Ищем класс размеров
                size_t size_class = 16;
                while (size_class < allocation_sizes[i]) {
                    size_class <<= 1;  // Умножаем на 2
                }
                total_allocated += size_class;
            } else if (algorithm == POWER_OF_2) {
                // Buddy system: размер + заголовок, округленный до степени двойки
                size_t rounded = next_power_of_2(allocation_sizes[i] + sizeof(BuddyBlock));
                total_allocated += rounded;
            }
        } else {  // Если выделение не удалось
            result.failed_allocations++;
            actual_sizes[i] = 0;
        }
    }

    // Среднее время выделения
    result.avg_allocation_time = total_alloc_time / num_allocations;

    // Вычисляем фрагментацию и эффективность
    if (total_requested > 0 && total_allocated > 0) {
        // Внутренняя фрагментация = выделено - запрошено
        result.internal_fragmentation = total_allocated - total_requested;
        // Эффективность = (запрошено / выделено) * 100%
        result.memory_efficiency = (double)total_requested / total_allocated * 100.0;
    }

    // Фаза освобождения памяти
    double total_dealloc_time = 0;
    for (size_t i = 0; i < num_allocations; i++) {
        if (allocated_ptrs[i]) {  // Если блок был выделен
            clock_t dealloc_start = clock();
            free_memory(allocator, allocated_ptrs[i], actual_sizes[i]);
            clock_t dealloc_end = clock();
            total_dealloc_time += (double)(dealloc_end - dealloc_start) / CLOCKS_PER_SEC;
        }
    }

    // Среднее время освобождения
    result.avg_deallocation_time = successful_allocations > 0
        ? total_dealloc_time / successful_allocations
        : 0.0;

    // Общее время теста
    clock_t end_time = clock();
    result.total_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;

    // Очистка
    free(allocated_ptrs);
    free(actual_sizes);
    destroy_allocator(allocator);

    return result;
}

void print_benchmark_results(const char* algorithm_name, BenchmarkResult result) {
    printf("\n=== %s Results ===\n", algorithm_name);
    printf("Average Allocation Time:   %.6f seconds\n", result.avg_allocation_time);
    printf("Average Deallocation Time: %.6f seconds\n", result.avg_deallocation_time);
    printf("Internal Fragmentation:    %zu bytes\n", result.internal_fragmentation);
    printf("Memory Efficiency:         %.2f%%\n", result.memory_efficiency);
    printf("Failed Allocations:        %zu\n", result.failed_allocations);
    printf("Total Time:                %.6f seconds\n", result.total_time);
    printf("===============================\n");
}

void compare_algorithms(size_t pool_size, size_t* allocation_sizes, size_t num_allocations) {
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║       Memory Allocation Algorithms Comparison                 ║\n");
    printf("║   McKusick-Karels vs Power-of-2 (Buddy System)                ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\nPool Size: %zu bytes\n", pool_size);
    printf("Number of Allocations: %zu\n", num_allocations);

    BenchmarkResult mk_result = benchmark_algorithm(MCKUSICK_KARELS, pool_size,
                                                     allocation_sizes, num_allocations);
    print_benchmark_results("McKusick-Karels", mk_result);

    BenchmarkResult p2_result = benchmark_algorithm(POWER_OF_2, pool_size,
                                                     allocation_sizes, num_allocations);
    print_benchmark_results("Power-of-2 (Buddy System)", p2_result);

    // Summary comparison
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                    Summary Comparison                         ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\n%-25s %-15s %-15s %-15s %-15s\n",
           "Algorithm", "Avg Alloc (s)", "Efficiency (%)", "Failed", "Total Time (s)");
    printf("────────────────────────────────────────────────────────────────────────────────────\n");
    printf("%-25s %-15.6f %-15.2f %-15zu %-15.6f\n",
           "McKusick-Karels", mk_result.avg_allocation_time,
           mk_result.memory_efficiency, mk_result.failed_allocations,
           mk_result.total_time);
    printf("%-25s %-15.6f %-15.2f %-15zu %-15.6f\n",
           "Power-of-2 (Buddy)", p2_result.avg_allocation_time,
           p2_result.memory_efficiency, p2_result.failed_allocations,
           p2_result.total_time);
    printf("────────────────────────────────────────────────────────────────────────────────────\n\n");

    // Анализы
    printf("Analysis:\n");
    if (mk_result.memory_efficiency > p2_result.memory_efficiency) {
        printf("  • McKusick-Karels shows better memory efficiency (%.2f%% vs %.2f%%)\n",
               mk_result.memory_efficiency, p2_result.memory_efficiency);
    } else {
        printf("  • Power-of-2 shows better memory efficiency (%.2f%% vs %.2f%%)\n",
               p2_result.memory_efficiency, mk_result.memory_efficiency);
    }

    if (mk_result.avg_allocation_time < p2_result.avg_allocation_time) {
        printf("  • McKusick-Karels is faster at allocation\n");
    } else {
        printf("  • Power-of-2 is faster at allocation\n");
    }

    if (mk_result.internal_fragmentation < p2_result.internal_fragmentation) {
        printf("  • McKusick-Karels has lower internal fragmentation\n");
    } else {
        printf("  • Power-of-2 has lower internal fragmentation\n");
    }
    printf("\n");
}
