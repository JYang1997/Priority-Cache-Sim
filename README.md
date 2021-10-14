Priority-Cache-Simulator
-------------------------------------------

Random sampling based cache replacement simulator.
On cache eviction, simulator randomly pick *K* objects, then evicts the object with smallest priority value.

To add a new replacement policy, in [src/priority.c](https://github.com/JYang1997/Priority-Cache-Sim/blob/main/src/priority.c) and [inc/priority.h](https://github.com/JYang1997/Priority-Cache-Sim/blob/main/inc/priority.h) add following for the new replacement:
* CreatePriority()
* UpdatePriorityOnHit()
* UpdatePriorityOnEvict()
* MinPriorityItem() 

Avialiable Policies: lru, mru, in-cache-lfu, perfect-lfu, lhd, hc, etc.

[example_main.c](https://github.com/JYang1997/Priority-Cache-Sim/blob/main/example_main.c) is an example code for how to use the simulator.

### compile and run example

```bash
make
```
```bash
./priority_cache input.txt sample_size policy_name
```
