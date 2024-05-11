#include "mem.h"
#include "stdlib.h"
#include "string.h"
#include <pthread.h>
#include <stdio.h>

static BYTE _ram[RAM_SIZE];

static struct
{
	uint32_t proc; // ID of process currently uses this page
	int index;	   // Index of the page in the list of pages allocated
				   // to the process.
	int next;	   // The next page in the list. -1 if it is the last
				   // page.
} _mem_stat[NUM_PAGES];

static pthread_mutex_t mem_lock;

void init_mem(void)
{
	memset(_mem_stat, 0, sizeof(*_mem_stat) * NUM_PAGES);
	memset(_ram, 0, sizeof(BYTE) * RAM_SIZE);
	pthread_mutex_init(&mem_lock, NULL);
}

/* get offset of the virtual address */
static addr_t get_offset(addr_t addr)
{
	return addr & ~((~0U) << OFFSET_LEN);
}

/* get the first layer index */
static addr_t get_first_lv(addr_t addr)
{
	return addr >> (OFFSET_LEN + PAGE_LEN);
}

/* get the second layer index */
static addr_t get_second_lv(addr_t addr)
{
	return (addr >> OFFSET_LEN) - (get_first_lv(addr) << PAGE_LEN);
}

/* Search for page table table from the a segment table */
static struct trans_table_t *get_trans_table(addr_t index, struct page_table_t *page_table)
{
	int i;
	for (i = 0; i < page_table->size; i++)
	{
		if (page_table->table[i].v_index == index)
		{
			return &page_table->table[i];
		}
	}
	return NULL;
}

/* Translate virtual address to physical address. If [virtual_addr] is valid,
 * return 1 and write its physical counterpart to [physical_addr].
 * Otherwise, return 0 */
static int translate(addr_t virtual_addr, addr_t *physical_addr, struct pcb_t *proc)
{
	/* Offset of the virtual address */
	addr_t offset = get_offset(virtual_addr);

	/* The first layer index */
	addr_t first_lv = get_first_lv(virtual_addr);

	/* The second layer index */
	addr_t second_lv = get_second_lv(virtual_addr);

	/* Search in the first level */
	struct trans_table_t *first_lv_table = get_trans_table(first_lv, proc->seg_table);
	if (first_lv_table == NULL)
	{
		return 0;
	}

	/* Search in the second level */
	struct trans_table_t *second_lv_table = get_trans_table(second_lv, first_lv_table->page_table);
	if (second_lv_table == NULL)
	{
		return 0;
	}

	/* Translate to physical address */
	*physical_addr = (second_lv_table->p_index << OFFSET_LEN) | offset;
	return 1;
}

addr_t alloc_mem(uint32_t size, struct pcb_t *proc)
{
	pthread_mutex_lock(&mem_lock);
	addr_t ret_mem = 0;

	uint32_t num_pages = (size % PAGE_SIZE) ? size / PAGE_SIZE : size / PAGE_SIZE + 1; // Number of pages we will use
	int mem_avail = 0;																   // We could allocate new memory region or not?

	/* First we must check if the amount of free memory in
	 * virtual address space and physical address space is
	 * large enough to represent the amount of required
	 * memory. If so, set 1 to [mem_avail].
	 * Hint: check [proc] bit in each page of _mem_stat
	 * to know whether this page has been used by a process.
	 * For virtual memory space, check bp (break pointer).
	 * */

	int i, count = 0;
	for (i = 0; i < NUM_PAGES; i++)
	{
		if (_mem_stat[i].proc == 0)
		{
			count++;
			if (count == num_pages)
			{
				mem_avail = 1;
				break;
			}
		}
		else
		{
			count = 0;
		}
	}

	if (mem_avail)
	{
		/* We could allocate new memory region to the process */
		ret_mem = proc->bp;
		proc->bp += num_pages * PAGE_SIZE;
		/* Update status of physical pages which will be allocated
		 * to [proc] in _mem_stat. Tasks to do:
		 *     - Update [proc], [index], and [next] field
		 *     - Add entries to segment table page tables of [proc]
		 *       to ensure accesses to allocated memory slot is
		 *       valid. */

		int start_index = i - count + 1;
		int end_index =``` i + 1;
		for (i = start_index; i <= end_index; i++)
		{
			_mem_stat[i].proc = proc->pid;
			_mem_stat[i].index = i - start_index;
			if (i == end_index)
			{
				_mem_stat[i].next = -1;
			}
			else
			{
				_mem_stat[i].next = i + 1;
			}

			/* Add entries to segment table page tables */
			addr_t virtual_addr = i << OFFSET_LEN;
			addr_t first_lv_index = get_first_lv(virtual_addr);
			addr_t second_lv_index = get_second_lv(virtual_addr);

			struct trans_table_t *first_lv_table = get_trans_table(first_lv_index, proc->seg_table);
			if (first_lv_table == NULL)
			{
				/* Allocate new page table */
				if (proc->seg_table->size == MAX_SEG)
				{
					pthread_mutex_unlock(&mem_lock);
					return 0;
				}

				int new_table_index = proc->seg_table->size;
				proc->seg_table->size++;

				first_lv_table = &proc->seg_table->table[new_table_index];
				first_lv_table->v_index = first_lv_index;
				first_lv_table->p_index = i;
				first_lv_table->next = -1;

				struct page_table_t *new_page_table = (struct page_table_t *)malloc(sizeof(struct page_table_t));
				if (new_page_table == NULL)
				{
					pthread_mutex_unlock(&mem_lock);
					return 0;
				}

				new_page_table->size = 0;
				first_lv_table->page_table = new_page_table;
			}

			struct trans_table_t *second_lv_table = get_trans_table(second_lv_index, first_lv_table->page_table);
			if (second_lv_table == NULL)
			{
				/* Allocate new page table */
				if (first_lv_table->page_table->size == MAX_SECOND_TABLE)
				{
					pthread_mutex_unlock(&mem_lock);
					return 0;
				}

				int new_table_index = first_lv_table->page_table->size;
				first_lv_table->page_table->size++;

				second_lv_table = &first_lv_table->page_table->table[new_table_index];
				second_lv_table->v_index = second_lv_index;
				second_lv_table->p_index = i;
				second_lv_table->next = -1;
			}
		}
	}
	pthread_mutex_unlock(&mem_lock);
	return ret_mem;
}

int free_mem(addr_t address, struct pcb_t *proc)
{
	/* DO NOTHING HERE. This mem is obsoleted */
	return 0;
}

int read_mem(addr_t address, struct pcb_t *proc, BYTE *data)
{
	addr_t physical_addr;
	if (translate(address, &physical_addr, proc))
	{
		*data = _ram[physical_addr];
		return 0;
	}
	else
	{
		return 1;
	}
}

int write_mem(addr_t address, struct pcb_t *proc, BYTE data)
{
	addr_t physical_addr;
	if (translate(address, &physical_addr, proc))
	{
		_ram[physical_addr] = data;
		return 0;
	}
	else
	{
		return 1;
	}
}

void dump(void)
{
	int i;
	for (i = 0; i < NUM_PAGES; i++)
	{
		if (_mem_stat[i].proc != 0)
		{
			printf("%03d: ", i);
			printf("%05x-%05x - PID: %02d (idx %03d, nxt: %03d)\n",
				   i << OFFSET_LEN,
				   ((i + 1) << OFFSET_LEN) - 1,
				   _mem_stat[i].proc,
				   _mem_stat[i].index,
				   _mem_stat[i].next);
			int j;
			for (j = i << OFFSET_LEN;
				 j < ((i + 1) << OFFSET_LEN) - 1;
				 j++)
			{

				if (_ram[j] != 0)
				{
					printf("\t%05x: %02x\n", j, _ram[j]);
				}
			}
		}
	}
}