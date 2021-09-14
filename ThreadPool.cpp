#include "ThreadPool.h"

ThreadPoolExecutor::ThreadPoolExecutor(int max_worker, std::vector<int32_t> cpu_core_list) {
    std::cout<<"inside ThreadPoolExecutor constructor"<<std::endl;
    this->max_worker = max_worker;
    this->cpu_core_list = cpu_core_list;
    for(size_t i = 0; i<this->max_worker; ++i) {
        workers.emplace_back(
            [this] {
                _pin_cpu_cores(this->cpu_core_list);
                while(true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->worker_mutex);
                        this->worker_condition.wait(lock,
                            [this]{ return this->stop || !this->tasks.empty(); });

                        // Measure time2
                        asm volatile ( "CPUID\n\t"
                                "RDTSC\n\t"
                                "mov %%edx, %0\n\t"
                                "mov %%eax, %1\n\t": "=r" (cycles_high2), "=r" (cycles_low2)::"%rax", "%rbx", "%rcx", "%rdx");

                        if(this->stop && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                    // Measure time3
                    asm volatile ( "CPUID\n\t"
                            "RDTSC\n\t"
                            "mov %%edx, %0\n\t"
                            "mov %%eax, %1\n\t": "=r" (cycles_high3), "=r" (cycles_low3)::"%rax", "%rbx", "%rcx", "%rdx");
                }
            }
        );
    }
}

std::mutex& ThreadPoolExecutor::get_mutex() {
    return this->worker_mutex;
}

std::condition_variable& ThreadPoolExecutor::get_condition() {
    return this->worker_condition;
}

bool ThreadPoolExecutor::is_stop() {
    return this->stop;
}

std::queue<std::function<void()>>& ThreadPoolExecutor::get_tasks() {
    return this->tasks;
}

ThreadPoolExecutor::~ThreadPoolExecutor() {
    std::cout<<"delete ThreadPoolExecutor"<<std::endl;
    {
        std::unique_lock<std::mutex> lock(this->worker_mutex);
        this->stop = true;
    }
    this->worker_condition.notify_all();
    for(std::thread &worker: this->workers)
        worker.join();
}

void _pin_cpu_cores(const std::vector<int32_t> &cpu_core_list) {
    // Create the OMP thread pool and bind to cores of cpu_pools one by one
    omp_set_num_threads(cpu_core_list.size());
    #pragma omp parallel num_threads(cpu_core_list.size())
    {
        // set the OMP thread affinity
        int thread_id = omp_get_thread_num(); // Suppose the ids are [0, 1, 2, 3] if the len of cpu_core_list is 4
        int phy_core_id = cpu_core_list[thread_id];
        kmp_affinity_mask_t mask;
        kmp_create_affinity_mask(&mask);
        kmp_set_affinity_mask_proc(phy_core_id, &mask);
        kmp_set_affinity(&mask);
        kmp_destroy_affinity_mask(&mask);
    }
    return;
}
