#include <syscall.h>
#include <arch.h>

error_t vm_link(struct task *task, vaddr_t vaddr, paddr_t paddr, paddr_t kpage,
                unsigned flags) {
    return OK;
}

void vm_unlink(struct task *task, vaddr_t vaddr) {
}

paddr_t vm_resolve(struct task *task, vaddr_t vaddr) {
    return 0;
}

void arch_memcpy_from_user(void *dst, userptr_t src, size_t len) {
}

void arch_memcpy_to_user(userptr_t dst, const void *src, size_t len) {
}