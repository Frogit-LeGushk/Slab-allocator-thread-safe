## Dynamic thread-safe allocator like as 'malloc'
```console
/***************************************
 * SLAB allocator                      *
 * Author: Acool4ik                    *
 *                                     *
 *      Params:                        *
 * Thread-safety                       *
 * Min blocks for allocate mem: 1Byte  *
 * Max blocks for allocate mem: 1GB    *
 *                                     *
 *      Complexity (API)               *
 * cache_setup: O(1)                   *
 * cache_release: O(K)                 *
 * cache_alloc: O(1*)                  *
 * cache_free: O(1)                    *
 * cache_shrink: O(K)                  *
 *                                     *
 * K - count of slabs                  *
 ***************************************/
```
### Then to run
```make && make run```
### Cache structure and slabs after initialize
```console
Cache [0x55b082a810a0][94216594657440]
	slab_order=10
	object_size=1048584
	cnt_objects=3
	meta_block_offset=3145752
	free_list_slabs	[0x55b083f00018]
	busy_list_slabs	[(nil)]
	part_list_slabs	[(nil)]
	
Free slab state:
Slab [0x55b083f00018][94216616149016]
Next slab [(nil)][0]
List of free blocks (3):
	[1][0x55b083c00000][94216613003264]
	[2][0x55b083d00008][94216614051848]
	[3][0x55b083e00010][94216615100432]
	
Partially busy slab state:
Slab [(nil)][0]
```
### Free and partial busy slabs after 2 allocations
```console
Free slab state:
Slab [(nil)][0]

Partially busy slab state:
Slab [0x55b083f00018][94216616149016]
Next slab [(nil)][0]
List of free blocks (1):
	[1][0x55b083e00010][94216615100432]
```
### Free and partial busy slabs after free (like as initial state)
```console
Free slab state:
Slab [0x55b083f00018][94216616149016]
Next slab [(nil)][0]
List of free blocks (3):
	[1][0x55b083d00008][94216614051848]
	[2][0x55b083c00000][94216613003264]
	[3][0x55b083e00010][94216615100432]

Partially busy slab state:
Slab [(nil)][0]
```
