/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include <bitmap.h>
#include <stdlib.h>

#include "devices/disk.h"
#include "threads/mmu.h"
#include "vm/vm.h"
/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
    .swap_in = anon_swap_in,
    .swap_out = anon_swap_out,
    .destroy = anon_destroy,
    .type = VM_ANON,
};

struct bitmap *swap_table;
size_t swap_size;

/* Initialize the data for anonymous pages */
void vm_anon_init(void) {
    /* TODO: Set up the swap_disk. */
    swap_disk = disk_get(1, 1);
    swap_size = disk_size(swap_disk) / SECTOR_SIZE;
    swap_table = bitmap_create(swap_size);
}

/** Project 3: Anonymous Page - Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva) {
    /* Set up the handler */
    /* 데이터를 모두 0으로 초기화 */
    struct uninit_page *uninit = &page->uninit;
    memset(uninit, 0, sizeof(struct uninit_page));

    page->operations = &anon_ops;

    struct anon_page *anon_page = &page->anon;
    /** Project 3: Swap In/Out - ERROR로 초기화  */
    anon_page->sector = BITMAP_ERROR;

    return true;
}

/** Project 3: Swap In/Out - Swap in the page by read contents from the swap disk. */
static bool anon_swap_in(struct page *page, void *kva) {
    struct anon_page *anon_page = &page->anon;
    size_t sector = anon_page->sector;

    if (sector == BITMAP_ERROR || !bitmap_test(swap_table, sector))
        return false;

    page->frame->kva = kva;

    bitmap_set(swap_table, sector / SECTOR_SIZE, false);

    for (size_t i = 0; i < SECTOR_SIZE; i++)
        disk_read(swap_disk, sector + i, page->frame->kva + DISK_SECTOR_SIZE * i);

    pml4_set_page(thread_current()->pml4, page->va, kva, true);

    sector = BITMAP_ERROR;

    return true;
}

/** Project 3: Swap In/Out - Swap out the page by writing contents to the swap disk. */
static bool anon_swap_out(struct page *page) {
    struct anon_page *anon_page = &page->anon;

    size_t free_idx = bitmap_scan_and_flip(swap_table, 0, 1, false);

    if (free_idx = BITMAP_ERROR)
        return false;

    size_t sector = anon_page->sector;

    for (size_t i = 0; i < SECTOR_SIZE; i++)
        disk_write(swap_disk, sector + i, page->frame->kva + DISK_SECTOR_SIZE * i);

    pml4_clear_page(thread_current()->pml4, page->va);

    page->frame->page = NULL;
    page->frame = NULL;

    return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void anon_destroy(struct page *page) {
    struct anon_page *anon_page = &page->anon;

    /** Project 3: Anonymous Page - 점거중인 frame 삭제 */
    if (page->frame) {
        list_remove(&page->frame->frame_elem);
        page->frame->page = NULL;
        page->frame = NULL;
        free(page->frame);
    }

    /** Project 3: Swap In/Out - 점거중인 bitmap 삭제 */
    if (page->anon.sector != BITMAP_ERROR)
        bitmap_reset(swap_table, page->anon.sector);
}
