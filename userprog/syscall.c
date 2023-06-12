#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void halt(void);
void exit(int status);
int write(int fd, const void *buffer, unsigned size);
void check_address(void *addr);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int add_file_to_fdt(struct file *f);
int read(int fd, void *buffer, unsigned size);
struct file *find_file_by_fd(int fd);
void close_file_by_fd(int fd);
void close(int fd);
int wait(int pid);
int exec(const char *cmd_line);
tid_t fork(const char *thread_name, struct intr_frame *f);
int file_size(int fd);
void seek(int fd, unsigned position);
unsigned tell(int fd);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

	void syscall_init(void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&filesys_lock);
	}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	int syscall_number = f->R.rax;

	switch (syscall_number)
	{
	case SYS_HALT:
		halt();
		break;

	case SYS_EXIT:
		exit(f->R.rdi);
		break;

	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;

	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;

	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;

	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	case SYS_CLOSE:
		close(f->R.rdi);
		break;

	case SYS_WAIT:
		f->R.rax = wait(f->R.rdi);
		break;

	case SYS_EXEC:
		f->R.rax = exec(f->R.rdi);
		break;

	case SYS_FORK:
		f->R.rax = fork(f->R.rdi, f);
		break;

	// 왜 쓰는지 이해 아직 못 함
	case SYS_FILESIZE:
		f->R.rax = file_size(f->R.rdi);
		break;

	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	}
}

void halt(void)
{
	power_off();
}

void exit(int status)
{
	struct thread *cur = thread_current();
	cur->exit_status = status;
	printf("%s: exit(%d)\n", cur->name, status);
	thread_exit();
}

int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);
	int write_result = 0;


	if (fd == 1)
	{
		putbuf(buffer, size);
		write_result = size;
	}
	else
	{
		if (fd < 2)
		{
			return -1;
		}

		struct file *fileobj = find_file_by_fd(fd);

		if (fileobj == NULL)
		{
			return -1;
		}
		lock_acquire(&filesys_lock);
		write_result = file_write(fileobj, buffer, size);
		lock_release(&filesys_lock);
	}

	return write_result;

	// if(fd == 1)
	// {
	// 	putbuf(buffer, size);
	// }
	// return size;
}

void check_address(void *addr)
{
	if (addr == NULL)
		exit(-1);
	if (!is_user_vaddr(addr))
		exit(-1);
	if (pml4_get_page(thread_current()->pml4, addr) == NULL)
		exit(-1);
}

bool create(const char *file, unsigned initial_size)
{
	check_address(file);
	return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
	check_address(file);
	return filesys_remove(file);
}

int open(const char *file_name)
{
	check_address(file_name);
	struct file *file = filesys_open(file_name);
	if (file == NULL)
		return -1;
	int fd = add_file_to_fdt(file);
	if (fd == -1)
		file_close(file);
	return fd;
}

int add_file_to_fdt(struct file *f)
{
	struct thread *curr = thread_current();
	struct file **fdt = curr->fdTable;
	// limit을 넘지 않는 범위 안에서 빈 자리 탐색
	while (curr->fdIdx < FDCOUNT_LIMIT && fdt[curr->fdIdx]){
		curr->fdIdx++;
	}
	if (curr->fdIdx >= FDCOUNT_LIMIT)
		return -1;
	fdt[curr->fdIdx] = f;
	return curr->fdIdx;
}

struct file *find_file_by_fd(int fd)
{
	if (fd < 2 || fd >= FDCOUNT_LIMIT)
	{
		return NULL;
	}
	struct thread *cur = thread_current();
	struct file **fdt = cur->fdTable;

	return fdt[fd];
}

void close(int fd)
{
	close_file_by_fd(fd);
}

void close_file_by_fd(int fd)
{
	if (fd < 0 || fd >= FDCOUNT_LIMIT)
	{
		return;
	}
	struct thread *cur = thread_current();
	struct file **fdt = cur->fdTable;

	fdt[fd] = NULL;
}



int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	char *ptr = (char *)buffer;
	int read_result = 0;

	lock_acquire(&filesys_lock);

	if (fd == 0)
	{
		for (int i = 0; i < size; i++)
		{
			*ptr++ = input_getc();
			read_result++;
		}
		lock_release(&filesys_lock);
	}
	// fd != 0 인 경우
	else
	{
		if (fd < 2)
		{
			lock_release(&filesys_lock);
			return -1;
		}

		struct file *fileobj = find_file_by_fd(fd);
		if (fileobj == NULL)
		{
			lock_release(&filesys_lock);
			return -1;
		}

		read_result = file_read(fileobj, buffer, size);
		lock_release(&filesys_lock);
	}

	return read_result;
}

int wait(int pid)
{
	return process_wait(pid);
}

int exec(const char *cmd_line)
{
	check_address(cmd_line);

	char *fn_copy;
	
	fn_copy = palloc_get_page(0);

	if (fn_copy == NULL)
	{
		exit(-1);
	}
	printf("\nhi1 : %s\n\n", fn_copy);
	// strlcpy(fn_copy, cmd_line, PGSIZE);
	printf("\nhi2\n\n");
	if (process_exec(fn_copy) == -1)
	{
		printf("\nhi3\n\n");
		exit(-1);
	}
}

tid_t fork(const char *thread_name, struct intr_frame *f)
{
	// printf("%s\n", thread_name);
	return process_fork(thread_name, f);
}

int file_size(int fd)
{
	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == NULL)
	{
		return -1;
	}

	return file_length(fileobj);
}

void seek(int fd, unsigned position)
{
	struct file *file = find_file_by_fd(fd);
	if (file == NULL)
		return;
	file_seek(file, position);
}

unsigned tell(int fd)
{
	struct file *file = find_file_by_fd(fd);
	if (file == NULL)
		return;
	return file_tell(file);
}