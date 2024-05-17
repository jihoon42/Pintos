/* vm.c: Generic interface for virtual memory objects. */
#include "vm/vm.h"

#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "vm/inspect.h"

static struct list frame_table;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void) {
    vm_anon_init();
    vm_file_init();
#ifdef EFILESYS /* For project 4 */
    pagecache_init();
#endif
    register_inspect_intr();
    /* DO NOT MODIFY UPPER LINES. */
    /* TODO: Your code goes here. */
    list_init(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type page_get_type(struct page *page) {
    int ty = VM_TYPE(page->operations->type);
    switch (ty) {
        case VM_UNINIT:
            return VM_TYPE(page->uninit.type);
        default:
            return ty;
    }
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/** Project 3: Anonymous Page - 이니셜라이저를 사용하여 보류 중인 페이지 개체를 만듭니다.
 *  페이지를 만들고 싶다면 직접 만들지 말고 이 함수나 `vm_alloc_page`를 통해 만들어주세요. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable, vm_initializer *init, void *aux) {
    ASSERT(VM_TYPE(type) != VM_UNINIT)

    struct supplemental_page_table *spt = &thread_current()->spt;

    /* Check wheter the upage is already occupied or not. */
    if (spt_find_page(spt, upage) == NULL) {
        /* TODO: Create the page, fetch the initialier according to the VM type,
         * TODO: and then create "uninit" page struct by calling uninit_new. You
         * TODO: should modify the field after calling the uninit_new. */
        struct page *page = (struct page *)malloc(sizeof(struct page));

        typedef bool (*initializerFunc)(struct page *, enum vm_type, void *);
        initializerFunc initializer = NULL;

        switch (VM_TYPE(type)) {
            case VM_ANON:
                initializer = anon_initializer;
                break;
            case VM_FILE:
                initializer = file_backed_initializer;
                break;
        }

        uninit_new(page, upage, init, type, aux, initializer);

        page->writable = writable;

        /* TODO: Insert the page into the spt. */
        return spt_insert_page(spt, page);
    }
err:
    return false;
}

/** Project 3: Memory Management - spt에서 va를 찾아 페이지를 리턴합니다. 오류가 발생하면 NULL을 반환합니다. */
struct page *spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
    /* TODO: Fill this function. */
    struct page *page = (struct page *)malloc(sizeof(struct page));     // 가상 주소에 대응하는 해시 값 도출을 위해 새로운 페이지 할당
    page->va = pg_round_down(va);                                       // 가상 주소의 시작 주소를 페이지의 va에 복제
    struct hash_elem *e = hash_find(&spt->spt_hash, &page->hash_elem);  // spt hash 테이블에서 hash_elem과 같은 hash를 갖는 페이지를 찾아서 return
    free(page);                                                         // 복제한 페이지 삭제

    if (e != NULL)
        return hash_entry(e, struct page, hash_elem);

    return NULL;
}

/** Project 3: Memory Management - 검증을 통해 spt에 PAGE를 삽입합니다. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
    /* TODO: Fill this function. */
    if (!hash_insert(&spt->spt_hash, &page->hash_elem))
        return true;

    return false;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page) {
    vm_dealloc_page(page);
    return true;
}

/** Project 3: Memory Management - 제거될 구조체 프레임을 가져옵니다. */
static struct frame *vm_get_victim(void) {
    struct frame *victim = NULL;
    /* TODO: The policy for eviction is up to you. */
    struct thread *curr = thread_current();
    struct list_elem *e = list_begin(&frame_table);

    // Second Chance 방식으로 결정
    for (e; e != list_end(&frame_table); e = list_next(e)) {
        victim = list_entry(e, struct frame, frame_elem);
        if (pml4_is_accessed(curr->pml4, victim->page->va))
            pml4_set_accessed(curr->pml4, victim->page->va, false);  // pml4가 최근에 사용됐다면 기회를 한번 더 준다.
        else
            return victim;
    }

    return victim;
}

/** Project 3: Memory Management - 한 페이지를 제거하고 해당 프레임을 반환합니다. 오류가 발생하면 NULL을 반환합니다.*/
static struct frame *vm_evict_frame(void) {
    struct frame *victim UNUSED = vm_get_victim();
    /* TODO: swap out the victim and return the evicted frame. */
    swap_out(victim->page);

    return victim;
}

/** Project 3: Memory Management - palloc()을 실행하고 프레임을 가져옵니다. 사용 가능한 페이지가 없으면 해당 페이지를 제거하고 반환합니다.
 *  이는 항상 유효한 주소를 반환합니다. 즉, 사용자 풀 메모리가 가득 찬 경우 이 함수는 사용 가능한 메모리 공간을 확보하기 위해 프레임을 제거합니다.*/
static struct frame *vm_get_frame(void) {
    /* TODO: Fill this function. */
    struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
    ASSERT(frame != NULL);

    frame->kva = palloc_get_page(PAL_USER);  // 유저 풀(실제 메모리)에서 페이지를 할당 받는다.

    if (frame->kva == NULL)
        frame = vm_evict_frame();  // Swap Out 수행
    else
        list_push_back(&frame_table, &frame->frame_elem);  // frame table에 추가

    frame->page = NULL;
    ASSERT(frame->page == NULL);

    return frame;
}

/* Growing the stack. */
static void vm_stack_growth(void *addr UNUSED) {
    bool success = false;
    if (vm_alloc_page(VM_ANON | VM_MARKER_0, addr, true)) {
        success = vm_claim_page(addr);

        if (success) {
            /* stack bottom size 갱신 */
            thread_current()->stack_bottom -= PGSIZE;
        }
    }
}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {
}

/** Project 3: Memory Management - Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED, bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
    struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
    struct page *page = NULL;
    /* TODO: Validate the fault */
    if (addr == NULL)
        return false;

    if (is_kernel_vaddr(addr))
        return false;

    if (!not_present)  // 접근한 메모리의 physical page가 존재하면 잘못됨
        return false;

    if (vm_claim_page(addr))  // demand page 수행
        return true;

    /** Project 3: Stack Growth */
    void *stack_pointer = is_kernel_vaddr(f->rsp) ? thread_current()->stack_pointer : f->rsp;
    /* stack pointer 아래 8바이트는 페이지 폴트 발생 & addr 위치를 USER_STACK에서 1MB로 제한 */
    if (stack_pointer - 8 <= addr && addr >= STACK_LIMIT && addr <= USER_STACK) {
        vm_stack_growth(thread_current()->stack_bottom - PGSIZE);
        return true;
    }

    return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page) {
    destroy(page);
    free(page);
}

/** Project 3: Memory Management - VA에 할당된 페이지를 요청하세요. */
bool vm_claim_page(void *va UNUSED) {
    /* TODO: Fill this function */
    struct page *page = spt_find_page(&thread_current()->spt, va);

    if (page == NULL)
        return false;

    return vm_do_claim_page(page);
}

/** Project 3: Memory Management - PAGE를 요청하고 mmu를 설정하십시오. */
static bool vm_do_claim_page(struct page *page) {
    struct frame *frame = vm_get_frame();

    /* Set links */
    frame->page = page;
    page->frame = frame;

    /* TODO: Insert page table entry to map page's VA to frame's PA. */
    pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable);

    return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED) {
    hash_init(&spt->spt_hash, hash_func, less_func, NULL);
}

/** Project 3: Anonymous Page - Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED, struct supplemental_page_table *src UNUSED) {
    struct hash_iterator iter;
    struct page *src_page;

    hash_first(&iter, &src->spt_hash);
    while (hash_next(&iter)) {
        src_page = hash_entry(hash_cur(&iter), struct page, hash_elem);

        if (src_page->operations->type == VM_UNINIT) {  // src 타입이 uninit인 경우
            if (!vm_alloc_page_with_initializer(page_get_type(src_page), src_page->va, src_page->writable, src_page->uninit.init, src_page->uninit.aux))
                return false;
            continue;
        }

        if (src_page->uninit.type & VM_MARKER_0) {  // src 페이지가 STACK인 경우
            setup_stack(&thread_current()->tf);
            goto done;
        }

        // src 타입이 anon인 경우
        if (!vm_alloc_page(page_get_type(src_page), src_page->va, src_page->writable))  // src를 unint 페이지로 만들고 spt 삽입
            return false;

        if (!vm_claim_page(src_page->va))  // 물리 메모리와 매핑하고 initialize 한다
            return false;

    done:  // UNIT이 아닌 모든 페이지에 대응하는 물리 메모리 데이터 복사
        struct page *dst_page = spt_find_page(dst, src_page->va);
        memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
    }

    return true;
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED) {
    hash_clear(&spt->spt_hash, hash_destructor);  // 해시 테이블의 모든 요소 제거
}