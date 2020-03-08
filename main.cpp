#include <numa.h>

#include <atomic>
#include <exception>
#include <thread>

#include <nonius/nonius.h++>

enum State
{
  Preparing,
  Ready,
  Ready2,
  Ping,
  Pong,
  Finish,
};

class Sync
{
public:
  State wait_as_long_as(State wait_state)
  {
    State loaded_state = state.load();
    while (loaded_state == wait_state)
      loaded_state = state.load();
    return loaded_state;
  }

  void wait_until(State expected_state)
  {
    while (state.load() != expected_state)
    {
    }
  }

  void set(State new_state)
  {
    state.store(new_state);
  }

private:
  std::atomic<State> state{Preparing};
};

static void set_affinity(unsigned int cpu_num)
{
  struct bitmask *cpumask = numa_allocate_cpumask();
  numa_bitmask_setbit(cpumask, cpu_num);
  numa_sched_setaffinity(0, cpumask);
  numa_free_cpumask(cpumask);
}

struct LatencyBench
{
  LatencyBench(long first_cpu_, long second_cpu_)
    : first_cpu{first_cpu_}
    , second_cpu{second_cpu_}
  {
  }

  void operator()(nonius::chronometer meter) const
  {
    set_affinity(first_cpu);

    numa_set_strict(1);
    Sync *sync = (Sync*) numa_alloc_local(sizeof(Sync));
    sync->set(Preparing);

    set_affinity(first_cpu);
    std::thread t1([&] {
      set_affinity(first_cpu);
      Sync *sync_ = sync;
      sync_->wait_until(Ready);

      meter.measure([&] {
        sync_->set(Ping);
        sync_->wait_until(Pong);
      });

      sync_->set(Finish);
    });

    set_affinity(second_cpu);
    std::thread t2([&] {
      set_affinity(second_cpu);
      Sync *sync_ = sync;
      sync_->wait_until(Ready2);
      sync_->set(Ready);

      State state = sync_->wait_as_long_as(Ready);
      while (state != Finish)
      {
        if (state == Ping)
          sync_->set(Pong);
        state = sync_->wait_as_long_as(Pong);
      }
    });

    numa_sched_setaffinity(0, numa_all_cpus_ptr);
    sync->set(Ready2);
    t1.join();
    t2.join();

    numa_free(sync, sizeof(Sync));
  }

  const long first_cpu;
  const long second_cpu;
};

int main()
{
  std::cout.setf(std::ios::unitbuf);
  const long num_cpus = sysconf(_SC_NPROCESSORS_ONLN);

  for (long i = 0; i < num_cpus; ++i) {
    for (long j = 0; j < num_cpus; ++j) {
      if (i == j) continue;
      nonius::global_benchmark_registry().emplace_back(
        "latency between CPU " + std::to_string(i) + " and " + std::to_string(j),
        LatencyBench(i, j));
    }
  }

  try
  {
    nonius::configuration conf = nonius::configuration{};
    conf.summary = true;
    conf.samples = 40;
    nonius::go(conf);
    return 0;
  }
  catch (const std::exception& exc)
  {
    std::cerr << "Error: " << exc.what() << '\n';
    return 1;
  }
  catch (...)
  {
    std::cerr << "Unknown error\n";
    return 1;
  }
}
