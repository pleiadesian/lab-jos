/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <kern/e1000.h>
#include <kern/spinlock.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.

	// LAB 3: Your code here.
	user_mem_assert(curenv, s, len, 0);

	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	// LAB 4: Your code here.
	// panic("sys_exofork not implemented");
	int r;
	struct Env *env = NULL;
	if ((r = env_alloc(&env, curenv->env_id)) < 0) 
		return r;

	env->env_status = ENV_NOT_RUNNABLE;
	env->env_tf = curenv->env_tf;
	env->env_brk = curenv->env_brk;
	env->env_tf.tf_regs.reg_eax = 0;  // return 0 in new env's sys_exofork
	return env->env_id;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	// LAB 4: Your code here.
	// panic("sys_env_set_status not implemented");
	int r;
	struct Env *env = NULL;
	if ((r = envid2env(envid, &env, true)) < 0) 
		return r;

	if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE) 
		return -E_INVAL;
	
	env->env_status = status;
	return 0;
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3), interrupts enabled, and IOPL of 0.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
	// panic("sys_env_set_trapframe not implemented");
	int r;
	struct Env *env = NULL;
	if ((r = envid2env(envid, &env, true)) < 0) 
		return r;

	env->env_tf = *tf;
	env->env_tf.tf_ds = GD_UD | 3;
	env->env_tf.tf_es = GD_UD | 3;
	env->env_tf.tf_ss = GD_UD | 3;
	env->env_tf.tf_cs = GD_UT | 3;
	env->env_tf.tf_eflags |= FL_IF;
	env->env_tf.tf_eflags = (env->env_tf.tf_eflags & ~FL_IOPL_MASK) | FL_IOPL_0;
	return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
	// panic("sys_env_set_pgfault_upcall not implemented");
	int r;
	struct Env *env = NULL;
	if ((r = envid2env(envid, &env, true)) < 0) 
		return r;

	env->env_pgfault_upcall = func;
	return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

	// LAB 4: Your code here.
	// panic("sys_page_alloc not implemented");
	int r;
	struct Env *env = NULL;
	if ((r = envid2env(envid, &env, true)) < 0) 
		return r;

	if ((uintptr_t)va >= UTOP || (uintptr_t)va % PGSIZE) 
		return -E_INVAL;

	if ((perm & ~PTE_SYSCALL) || !(perm & PTE_U) || !(perm & PTE_P)) 
		return -E_INVAL;

	struct PageInfo *pp = page_alloc(ALLOC_ZERO);
	if (pp == NULL) 
		return -E_NO_MEM;

	if ((r = page_insert(env->env_pgdir, pp, va, perm)) < 0) {
		page_free(pp);
		return r;
	}

	if ((r = page_insert(env->env_kern_pgdir, pp, va, perm)) < 0) {
		page_remove(env->env_pgdir, va);
		page_free(pp);
		return r;
	}

	struct PageInfo *upp = page_lookup(env->env_pgdir, va, NULL);
	struct PageInfo *kpp = page_lookup(env->env_kern_pgdir, va, NULL);
	assert(upp != NULL);
	assert(kpp != NULL);
	assert(upp->pp_ref == 2 && kpp->pp_ref == 2);
	assert(upp == kpp);
		
	return 0;
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// LAB 4: Your code here.
	// panic("sys_page_map not implemented");
    int r;
	struct Env *srcenv = NULL;
	struct Env *dstenv = NULL;
	if ((r = envid2env(srcenvid, &srcenv, true)) < 0) 
		return r;

	if ((r = envid2env(dstenvid, &dstenv, true)) < 0) 
		return r;

	if ((uintptr_t)srcva >= UTOP || (uintptr_t)srcva % PGSIZE ||
		(uintptr_t)dstva >= UTOP || (uintptr_t)dstva % PGSIZE)
		return -E_INVAL;

	pte_t *pte = NULL;
	struct PageInfo *srcpp = page_lookup(srcenv->env_pgdir, srcva, &pte);
	if (srcpp == NULL) 
		return -E_INVAL;

	if ((perm & ~PTE_SYSCALL) || !(perm & PTE_U) || !(perm & PTE_P) ||
		(!(*pte | PTE_W) && (perm & PTE_W)))
		return -E_INVAL;

	if ((r = page_insert(dstenv->env_pgdir, srcpp, dstva, perm)) < 0) {
		page_free(srcpp);
		return r;
	}

	if ((r = page_insert(dstenv->env_kern_pgdir, srcpp, dstva, perm)) < 0) {
		page_remove(dstenv->env_pgdir, dstva);
		page_free(srcpp);
		return r;
	}

	return 0;
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().

	// LAB 4: Your code here.
	// panic("sys_page_unmap not implemented");
	int r;
	struct Env *env = NULL;
	if ((r = envid2env(envid, &env, true)) < 0) 
		return r;

	if ((uintptr_t)va >= UTOP || (uintptr_t)va % PGSIZE) 
		return -E_INVAL;

	struct PageInfo *pp = page_lookup(env->env_pgdir, va, NULL);
	assert (pp == page_lookup(env->env_kern_pgdir, va, NULL));
	bool ass = false;
	if (pp && pp->pp_ref == 2) {
		ass = true;
	}
	if (ass) assert(pp->pp_ref == 2);
	page_remove(env->env_pgdir, va);
	if (ass) assert(pp->pp_ref == 1);
	page_remove(env->env_kern_pgdir, va);
	if (ass) assert(pp->pp_ref == 0);
	return 0;
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
	// panic("sys_ipc_try_send not implemented");
	int r;
	struct Env *env = NULL;
	if ((r = envid2env(envid, &env, false)) < 0) 
		panic("envid2env: %e", r);

	if (env->env_ipc_recving == 0) { 
		// return -E_IPC_NOT_RECV;
		// not waked up until received
		curenv->env_ipc_sending = 1;
		curenv->env_status = ENV_NOT_RUNNABLE;
		curenv->env_ipc_send_envid = envid;
		curenv->env_ipc_send_value = value;
		curenv->env_ipc_srcva = srcva;
		curenv->env_ipc_send_perm = perm;
		sched_yield();
		return 0;
	}

	if (srcva != NULL && (uintptr_t)srcva < UTOP && env->env_ipc_dstva != NULL &&
		(uintptr_t)env->env_ipc_dstva < UTOP) {
		if ((uintptr_t)srcva % PGSIZE) 
			return -E_INVAL;
		if ((perm & ~PTE_SYSCALL) || !(perm & PTE_U) || !(perm & PTE_P)) 
			return -E_INVAL;
		pte_t *pte = NULL;
		struct PageInfo *pp = page_lookup(curenv->env_pgdir, srcva, &pte);
		if (pp == NULL)
			return -E_INVAL;
		if ((perm & PTE_W) && !(*pte & PTE_W)) 
			return -E_INVAL;		
		if ((r = page_insert(env->env_pgdir, pp, env->env_ipc_dstva, perm)) < 0) 
			return r;
		if ((r = page_insert(env->env_kern_pgdir, pp, env->env_ipc_dstva, perm)) < 0) {
			page_remove(env->env_pgdir, env->env_ipc_dstva);
			return r;
		}
		env->env_ipc_perm = perm;
	} else{
		env->env_ipc_perm = 0;
	}
	env->env_ipc_recving = 0;
	env->env_ipc_from = curenv->env_id;
	env->env_ipc_value = value;
	env->env_status = ENV_RUNNABLE;
	env->env_tf.tf_regs.reg_eax = 0;
	return 0;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
	// panic("sys_ipc_recv not implemented");
	int r;
	if (dstva != NULL && (uintptr_t)dstva < UTOP) {
		if ((uintptr_t)dstva % PGSIZE) 
			return -E_INVAL;
		curenv->env_ipc_dstva = dstva;
	} else {
		curenv->env_ipc_dstva = NULL;
	}

	// traverse all envs to find pending senders
	for (int i = 0; i < NENV; i++) {
		if (envs[i].env_ipc_sending == 1 && envs[i].env_ipc_send_envid == curenv->env_id) {
			void *srcva = envs[i].env_ipc_srcva;
			int perm = envs[i].env_ipc_send_perm;
			if (srcva != NULL && (uintptr_t)srcva < UTOP && dstva != NULL && (uintptr_t)dstva < UTOP) {
				if ((uintptr_t)srcva % PGSIZE) {
					envs[i].env_tf.tf_regs.reg_eax = -E_INVAL;
					envs[i].env_ipc_sending = 0;
					envs[i].env_status = ENV_RUNNABLE;
					continue;
				}
				if ((perm & ~PTE_SYSCALL) || !(perm & PTE_U) || !(perm & PTE_P)) {
					envs[i].env_tf.tf_regs.reg_eax = -E_INVAL;
					envs[i].env_ipc_sending = 0;
					envs[i].env_status = ENV_RUNNABLE;
					continue;
				}
				pte_t *pte = NULL;
				struct PageInfo *pp = page_lookup(envs[i].env_pgdir, srcva, &pte);
				if (pp == NULL) {
					envs[i].env_tf.tf_regs.reg_eax = -E_INVAL;
					envs[i].env_ipc_sending = 0;
					envs[i].env_status = ENV_RUNNABLE;
					continue;
				}
				if ((perm & PTE_W) && !(*pte & PTE_W)) {
					envs[i].env_tf.tf_regs.reg_eax = -E_INVAL;
					envs[i].env_ipc_sending = 0;
					envs[i].env_status = ENV_RUNNABLE;
					continue;
				}
				if ((r = page_insert(curenv->env_pgdir, pp, dstva, perm)) < 0) {
					envs[i].env_tf.tf_regs.reg_eax = r;
					envs[i].env_ipc_sending = 0;
					envs[i].env_status = ENV_RUNNABLE;
					continue;
				}
				if ((r = page_insert(curenv->env_kern_pgdir, pp, dstva, perm)) < 0) {
					page_remove(curenv->env_pgdir, dstva);
					envs[i].env_tf.tf_regs.reg_eax = r;
					envs[i].env_ipc_sending = 0;
					envs[i].env_status = ENV_RUNNABLE;
					continue;
				}
				curenv->env_ipc_perm = perm;
			} else {
				curenv->env_ipc_perm = 0;
			}
			curenv->env_ipc_recving = 0;
			curenv->env_ipc_from = envs[i].env_id;
			curenv->env_ipc_value = envs[i].env_ipc_send_value;
			envs[i].env_ipc_sending = 0;
			envs[i].env_status = ENV_RUNNABLE;
			envs[i].env_tf.tf_regs.reg_eax = 0;
			return 0;
		}
	}

	curenv->env_ipc_recving = 1;
	curenv->env_status = ENV_NOT_RUNNABLE;
	sched_yield();
	return 0;
}

static int
sys_map_kernel_page(void* kpage, void* va)
{
    int r;
    struct PageInfo* p = pa2page(PADDR(kpage));
    if (p == NULL)
        return -E_INVAL;
    if ((r = page_insert(curenv->env_pgdir, p, va, PTE_U | PTE_W)) < 0) {
		return r;
	}
	if ((r = page_insert(curenv->env_kern_pgdir, p, va, PTE_U | PTE_W)) < 0) {
		page_remove(curenv->env_pgdir, va);
		return r;
	}
    return 0;
}

static int
sys_sbrk(uint32_t inc)
{
    // LAB3: your code here.
	uintptr_t brk_inc = ROUNDUP(curenv->env_brk + inc, PGSIZE);
	if (brk_inc >= USTACKTOP) 
		panic("sys_sbrk: out of memory");

	int r;
	for (uintptr_t i = curenv->env_brk; i < brk_inc; i += PGSIZE) {
		struct PageInfo *page = page_alloc(0);
		if (page == NULL) 
			panic("sys_sbrk: page_alloc failed");
		if ((r = page_insert(curenv->env_pgdir, page, (void *)i, PTE_W | PTE_U)) < 0) 
			panic("sys_sbrk: %e", r); 
		if ((r = page_insert(curenv->env_kern_pgdir, page, (void *)i, PTE_W | PTE_U)) < 0) 
			panic("sys_sbrk: %e", r); 
	}

	curenv->env_brk = brk_inc;
    return brk_inc;
}

// Return the current time.
static int
sys_time_msec(void)
{
	// LAB 6: Your code here.
	// panic("sys_time_msec not implemented");
	return time_msec();
}

int
sys_net_send(const void *buf, uint32_t len)
{
	// LAB 6: Your code here.
	// Check the user permission to [buf, buf + len]
	// Call e1000_tx to send the packet
	// Hint: e1000_tx only accept kernel virtual address
	int r;
	if (len <= 0 || len > TX_PACKET_SIZE) 
		return -E_INVAL;
	if ((r = user_mem_check(curenv, buf, len, PTE_U)) < 0) 
		return r;
	pte_t *pte = pgdir_walk(curenv->env_pgdir, buf, false);
	if (pte == NULL) 
		panic("e1000_tx: pgdir_walk failed");
	if ((r = e1000_tx(buf, len)) < 0) 
		return r;
	return 0;
}

int
sys_net_recv(void *buf, uint32_t len)
{
	// LAB 6: Your code here.
	// Check the user permission to [buf, buf + len]
	// Call e1000_rx to fill the buffer
	// Hint: e1000_rx only accept kernel virtual address
	int r;
	if (len <= 0 || len > RX_PACKET_SIZE) 
		return -E_INVAL;
	if ((r = user_mem_check(curenv, buf, len, PTE_U | PTE_W)) < 0) 
		return r;
	return e1000_rx(buf, len);
}

#ifdef ZERO_COPY
int
sys_net_tdt()
{
	// get E1000 TDT value
	return get_tdt();
}

int
sys_net_rdt()
{
	// get E1000 TDT value
	return get_rdt();
}
#endif

// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.

	// panic("syscall not implemented");
	int32_t ret;
	// lock_kernel();
	switch (syscallno) {
		case SYS_cputs: {
			sys_cputs((char *)a1, a2);
			ret = 0;
			break;
		}
		case SYS_cgetc: {
			ret = sys_cgetc();
			break;
		}
		case SYS_getenvid: {
			ret = sys_getenvid();
			break;
		}
		case SYS_env_destroy: {
			ret = sys_env_destroy(a1);
			break;
		}
		case SYS_yield: {
			sys_yield();
			ret = 0;
			break;
		}
		case SYS_exofork: {
			ret = sys_exofork();
			break;
		}
		case SYS_env_set_status: {
			ret = sys_env_set_status(a1, a2);
			break;
		}
		case SYS_env_set_trapframe: {
			ret = sys_env_set_trapframe(a1, (void*)a2);
			break;
		}
		case SYS_env_set_pgfault_upcall: {
			ret = sys_env_set_pgfault_upcall(a1, (void*)a2);
			break;
		}
		case SYS_page_alloc: {
			ret = sys_page_alloc(a1, (void*)a2, a3);
			break;
		}
		case SYS_page_map: {
			ret = sys_page_map(a1, (void*)a2, a3, (void*)a4, a5);
			break;
		}
		case SYS_page_unmap: {
			ret = sys_page_unmap(a1, (void*)a2);
			break;
		}
		case SYS_ipc_try_send: {
			ret = sys_ipc_try_send(a1, a2, (void*)a3, a4);
			break;
		}
		case SYS_ipc_recv: {
			ret = sys_ipc_recv((void*)a1);
			break;
		}
		case SYS_map_kernel_page: {
			ret = sys_map_kernel_page((void*)a1, (void*)a2);
			break;
		}
		case SYS_sbrk: {
			ret = sys_sbrk(a1);
			break;
		}
		case SYS_time_msec: {
			ret = sys_time_msec();
			break;
		}
		case SYS_net_send: {
			ret = sys_net_send((void*)a1, a2);
			break;
		}
		case SYS_net_recv: {
			ret = sys_net_recv((void*)a1, a2);
			break;
		}
#ifdef ZERO_COPY
		case SYS_net_tdt: {
			ret = sys_net_tdt();
			break;
		}
		case SYS_net_rdt: {
			ret = sys_net_rdt();
			break;
		}
#endif
		default:
			ret = -E_INVAL;
	}
	// unlock_kernel();
	return ret;
}

