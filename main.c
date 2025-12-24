#include "memory_allocation.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    const char* name;
    BenchmarkResult result;
} AlgoResult;

static int write_benchmark_csv(const char* filename, AlgoResult* results, size_t count) {
    FILE* f = fopen(filename, "w");
    if (!f) {
        perror("Failed to open CSV for writing");
        return -1;
    }

    // Header ожидается визуализацией
    fprintf(f,
            "algorithm,avg_allocation_time,avg_deallocation_time,"
            "memory_efficiency,internal_fragmentation,failed_allocations,total_time\n");

    for (size_t i = 0; i < count; i++) {
        fprintf(f, "%s,%.10f,%.10f,%.4f,%zu,%zu,%.10f\n",
                results[i].name,
                results[i].result.avg_allocation_time,
                results[i].result.avg_deallocation_time,
                results[i].result.memory_efficiency,
                results[i].result.internal_fragmentation,
                results[i].result.failed_allocations,
                results[i].result.total_time);
    }

    fclose(f);
    return 0;
}

int main(void) {
    srand((unsigned int)time(NULL));

    // Конфигурация
    size_t pool_size = 1024 * 1024; // 1 MB
    size_t num_allocations = 1000;

    // Генерация случайных размеров
    size_t* allocation_sizes = (size_t*)malloc(num_allocations * sizeof(size_t));
    if (!allocation_sizes) {
        fprintf(stderr, "Failed to allocate memory for test sizes\n");
        return 1;
    }
    for (size_t i = 0; i < num_allocations; i++) {
        allocation_sizes[i] = 16 + (rand() % 4080); // 16..4096 bytes
    }

    // Запуск бенчмарков
    BenchmarkResult mk_result = benchmark_algorithm(MCKUSICK_KARELS, pool_size,
                                                    allocation_sizes, num_allocations);
    BenchmarkResult p2_result = benchmark_algorithm(POWER_OF_2, pool_size,
                                                    allocation_sizes, num_allocations);

    AlgoResult results[] = {
        {"McKusick-Karels", mk_result},
        {"Power-of-2 (Buddy)", p2_result},
    };

    const char* csv_path = "benchmark_results.csv";
    if (write_benchmark_csv(csv_path, results, 2) != 0) {
        free(allocation_sizes);
        return 1;
    }

    printf("✓ Benchmark results saved to %s\n", csv_path);

    free(allocation_sizes);
    return 0;
}