/**
 * Provides Operating System specific functions:
 * file access, sockets, signals, timers, async I/O, workers (threads)
 */
declare module "os" {
	type Success = 0;
	type Errno = number;
	type NegativeErrno = number;
	type Result<T> = T | NegativeErrno;
	type ExitStatus = number;
	type WaitStatus = number;
	type Branded<T, B> = T & { __brand: B }; // Prevent interchangeable primitives (e.g. number)
	type OpenOption = Branded<number, "OpenOption">;
	type SocketFamily = Branded<number, "SocketFamily">;
	type SocketType = Branded<number, "SocketType">;
	type SocketOpt = Branded<number, "SocketOpt">;
	type SocketShutOpt = Branded<number, "SocketShutOpt">;
	type PollOpt = Branded<number, "PollOpt">;
	type PollRet = Branded<number, "PollRet">;
	type WaitpidOpt = Branded<number, "WaitpidOpt">;
	type FileDescriptor = Branded<number, "FileDescriptor">;
	type Signal = Branded<number, "Signal">;
	type StatMode = Branded<number, "StatMode">;
	type Pid = Branded<number, "Pid">;
	type TimerHandle = Branded<unknown, "TimerHandle">;
	type Callback = () => void;
	type Platform = "linux" | "darwin" | "win32" | "js";
	type WorkerMessage = any;

	interface ExecOptions {
		/**
		 * Boolean (default = `true`). If `true`, wait until the process is
		 * terminated. In this case, exec return the exit code if positive or the
		 * negated signal number if the process was interrupted by a signal. If
		 * false, do not block and return the process id of the child.
		 */
		block?: boolean;
		/** Is the file searched in the PATH @default true. */
		usePath?: boolean;
		/** Set the file to be executed @default args[0] */
		file?: string,
		/** The working directory of the new process */
		cwd?: string,
		/** Handler for stdin */
		stdin?: FileDescriptor,
		/** Handler for stdout */
		stdout?: FileDescriptor,
		/** Handler for stderr */
		stderr?: FileDescriptor,
		/** set the process environment from the object key-value pairs.
		 * If unset, it will inherit them from current process. */
		env?: { [key: string]: string },
		/** Process uid with `setuid` */
		uid?: number,
		/** Process gid with `setgid` */
		gid?: number,
	}
	type ExecNonBlockingOptions = ExecOptions & { block?: false };
	type ExecBlockingOptions = ExecOptions & { block: true };

	interface Stat {
		dev: number;
		ino: number;
		mode: StatMode;
		nlink: number;
		uid: number;
		gid: number;
		rdev: number;
		size: number;
		blocks: number;
		/** milliseconds since 1970 */
		atime: number;
		/** milliseconds since 1970 */
		mtime: number;
		/** milliseconds since 1970 */
		ctime: number;
	}
	interface SocketAddr {
		/** @example 80 */
		port: number;
		/** @example "8.8.8.8" or "::1" */
		addr: string;
		/** @default AF_INET */
		family: SocketFamily;
	}
	interface SocketAddrInfo extends SocketAddr {
		socktype: SocketType;
	}
	interface HintAddr {
		/** @example 80 or "http" */
		service?: number | string;
		/** @default any */
		family?: SocketFamily;
		/** @default any */
		socktype?: SocketType;
	}
	/* POSIX constants (Windows-specific) */
	export const O_TEXT: OpenOption;
	/* POSIX constants */
	export const O_APPEND: OpenOption;
	export const O_CREAT: OpenOption;
	export const O_EXCL: OpenOption;
	export const O_RDONLY: OpenOption;
	export const O_RDWR: OpenOption;
	export const O_TRUNC: OpenOption;
	export const O_WRONLY: OpenOption;
	export const S_IFBLK: StatMode;
	export const S_IFCHR: StatMode;
	export const S_IFDIR: StatMode;
	export const S_IFIFO: StatMode;
	export const S_IFLNK: StatMode;
	export const S_IFMT: StatMode;
	export const S_IFREG: StatMode;
	export const S_IFSOCK: StatMode;
	export const S_ISGID: StatMode;
	export const S_ISUID: StatMode;
	export const SIGABRT: Signal;
	export const SIGALRM: Signal;
	export const SIGCHLD: Signal;
	export const SIGCONT: Signal;
	export const SIGFPE: Signal;
	export const SIGILL: Signal;
	export const SIGINT: Signal;
	export const SIGPIPE: Signal;
	export const SIGQUIT: Signal;
	export const SIGSEGV: Signal;
	export const SIGSTOP: Signal;
	export const SIGTERM: Signal;
	export const SIGTSTP: Signal;
	export const SIGTTIN: Signal;
	export const SIGTTOU: Signal;
	export const SIGUSR1: Signal;
	export const SIGUSR2: Signal;
	export const WNOHANG: WaitpidOpt;
	export const AF_INET: SocketFamily;
	export const AF_INET6: SocketFamily;
	export const SOCK_STREAM: SocketType;
	export const SOCK_DGRAM: SocketType;
	export const SOCK_RAW: SocketType;
	//export const SOCK_BLOCK: SocketType; // SOCK_NONBLOCK
	export const SO_REUSEADDR: SocketOpt;
	export const SO_ERROR: SocketOpt;
	export const SO_RCVBUF: SocketOpt;
	export const SHUT_RD: SocketShutOpt;
	export const SHUT_WR: SocketShutOpt;
	export const SHUT_RDWR: SocketShutOpt;
	/** string representing the platform. */
	export const platform: Platform;
	/** Open a file. Return a handle or `< 0` if error. */
	export function open(filename: string, flags?: OpenOption, mode?: number): Result<FileDescriptor>;
	/** Close the file handle `fd`. */
	export function close(fd: FileDescriptor): Result<Success>;
	/** Seek in the file. Use `std.SEEK_*` for whence */
	export function seek(fd: FileDescriptor, offset: number, whence: number): Result<number>;
	export function seek(fd: FileDescriptor, offset: bigint, whence: number): Result<bigint>;
	/** Read `length` bytes from the file handle `fd` to the `ArrayBuffer` buffer at byte position `offset` */
	export function read(fd: FileDescriptor, buffer: ArrayBuffer, offset: number, length: number): Result<number>;
	/** Write `length` bytes to the file handle `fd` from the ArrayBuffer `buffer` at byte position `offset` */
	export function write(fd: FileDescriptor, buffer: ArrayBuffer, offset: number, length: number): Result<number>;
	/** Return `true` is fd is a TTY (terminal) handle. */
	export function isatty(fd: FileDescriptor): boolean;
	/** Return the TTY size as `[width, height]` or `null` if not available. */
	export function ttyGetWinSize(fd: FileDescriptor): [width: number, height: number] | null;
	/** Set the TTY in raw mode. */
	export function ttySetRaw(fd: FileDescriptor): void;
	/** Remove a file. */
	export function remove(filename: string): Result<Success>;
	/** Rename a file. */
	export function rename(filename: string): Result<Success>;
	/** Get the canonicalized absolute pathname of `path` */
	export function realpath(path: string): [absPath: string, code: Success | Errno];
	/** Return the current working directory */
	export function getcwd(): [cwd: string, code: Success | Errno];
	/** Change the current directory. Return 0 if OK or `-errno`. */
	export function chdir(): Result<Success>;
	/** Create a directory at `path`. Return 0 if OK or `-errno`. */
	export function mkdir(path: string, mode?: number): Result<Success>;
	/** Get a file status */
	export function stat(path: string): [fileStatus: Stat, code: Success | Errno]
	/** Get a link status */
	export function lstat(path: string): [linkStatus: Stat, code: Success | Errno]
	/** Change the access and modification times of the file `path` @returns ms since 1970. */
	export function utimes(path: string, atime: number, mtime: number): Result<Success>;
	/** Create a link at `linkpath` containing the string `target`. */
	export function symlink(target: string, linkpath: string): Result<Success>;
	/** Get link target */
	export function readlink(path: string): [linkTarget: string, code: Success | Errno];
	/** List directory entries */
	export function readdir(dirPath: string): [dirFilenames: string[], code: Success | Errno];
	/** Set the single `func` read handler to be called each time data can be written to `fd`. */
	export function setReadHandler(fd: FileDescriptor, func: Callback): void;
	/** Remove the read handler for `fd`. */
	export function setReadHandler(fd: FileDescriptor, func: null): void;
	/** Set the single `func` read handler to be called each time data can be written to `fd`. */
	export function setWriteHandler(fd: FileDescriptor, func: Callback): void;
	/** Remove the write handler for `fd`. */
	export function setWriteHandler(fd: FileDescriptor, func: null): void;
	/** Set the single `func` to be called when `signal` happens. Work in main thread only */
	export function signal(signal: Signal, func: Callback): void
	/** Call the default handler when `signal` happens. */
	export function signal(signal: Signal, func: null): void
	/** Ignore when `signal` happens. */
	export function signal(signal: Signal, func: undefined): void
	/** Send the signal `sig` to the process `pid`. */
	export function kill(pid: Pid, signal: Signal): Result<number>;
	/** Execute a process with the arguments args. */
	export function exec(args: string[], options?: ExecBlockingOptions): Result<ExitStatus>;
	/** Execute a process with the arguments args. */
	export function exec(args: string[], options: ExecNonBlockingOptions): Result<Pid>;
	/** `waitpid` Unix system call. */
	export function waitpid(pid: Pid, options: WaitpidOpt): [ret: Result<Pid | Success>, status: WaitStatus];
	/** `getpid` Unix system call. */
	export function getpid(): [Pid];
	/** `dup` Unix system call. */
	export function dup(fd: FileDescriptor): Result<FileDescriptor>;
	/** `dup2` Unix system call. */
	export function dup2(oldFd: FileDescriptor, newFd: FileDescriptor): Result<FileDescriptor>;
	/** `pipe` Unix system call. */
	export function pipe(): [readFd: FileDescriptor, writeFd: FileDescriptor] | null;
	/** Sleep during `delay_ms` milliseconds. */
	export function sleep(delay_ms: number): Result<number>;
	/** Sleep during `delay_ms` milliseconds. */
	export function sleepAsync(delay_ms: number): Promise<Result<number>>;
	/** Call the function func after `delay` ms. */
	export function setTimeout(func: Callback, delay: number): TimerHandle;
	/** Cancel a timer. */
	export function clearTimeout(handle: TimerHandle): void;
	/** Create a POSIX socket */
	export function socket(family: SocketFamily, type: SocketType): Result<FileDescriptor>;
	/** Get a socket option @example os.getsockopt(sock_srv, os.SO_RCVBUF, uintArr1.buffer); */
	export function getsockopt(sockfd: FileDescriptor, name: SocketOpt, data: ArrayBuffer): Result<Success>;
	/** Set a socket option @example os.setsockopt(sock_srv, os.SO_REUSEADDR, new Uint32Array([1]).buffer); */
	export function setsockopt(sockfd: FileDescriptor, name: SocketOpt, data: ArrayBuffer): Result<Success>;
	/** Get address information for a given node and/or service @example os.getaddrinfo("localhost", {family:os.AF_INET6}) */
	export function getaddrinfo(node?: string, hint?: HintAddr): Result<Array<SocketAddrInfo>>;
	/** Get current address to which the socket sockfd is bound */
	export function getsockname(sockfd: FileDescriptor): Result<SocketAddr>;
	/** Bind socket to a specific address */
	export function bind(sockfd: FileDescriptor, addr: SocketAddr): Result<Success>;
	/** Mark `sockfd` as passive socket that will `accept()` a `backlog` number of incoming connection (SOMAXCONN by default). */
	export function listen(sockfd: FileDescriptor, backlog?: number): Result<Success>;
	/** Shut down part of a full-duplex connection */
	export function shutdown(sockfd: FileDescriptor, type: SocketShutOpt): Result<Success>;
	/** Accept incoming connections */
	export function accept(sockfd: FileDescriptor): Promise<[remotefd: FileDescriptor, remoteaddr: SocketAddr]>;
	/** Connect `sockfd` to `addr` */
	export function connect(sockfd: FileDescriptor, addr: SocketAddr): Promise<Result<Success>>;
	/** Send `length` byte from `buffer` on `sockfd` @returns bytes sent or <0 if error */
	export function send(sockfd: FileDescriptor, buffer: ArrayBuffer, length?: number): Promise<Result<number>>;
	/** Receive `length` byte in `buffer` from `sockfd` @returns bytes received or <0 if error */
	export function recv(sockfd: FileDescriptor, buffer: ArrayBuffer, length?: number): Promise<Result<number>>;
	/** Send `length` byte from `buffer` on `sockfd` to `addr` @returns bytes sent or <0 if error */
	export function sendto(sockfd: FileDescriptor, addr: SocketAddr, buffer: ArrayBuffer, length?: number): Promise<Result<number>>;
	/** Receive `length` byte in `buffer` from `sockfd` @returns bytes received or <0 if error, and remote address used */
	export function recvfrom(sockfd: FileDescriptor, buffer: ArrayBuffer, length?: number): Promise<[total: Result<number>, from: SocketAddr]>;

	export class Worker {
		/**
		 * In the created worker, `Worker.parent` represents the parent worker
		 * and is used to send or receive messages.
		 */
		static parent?: Worker;
		/**
		 * Constructor to create a new thread (worker) with an API close to
		 * the `WebWorkers`. `module_filename` is a string specifying the
		 * module filename which is executed in the newly created thread. As
		 * for dynamically imported module, it is relative to the current
		 * script or module path. Threads normally don’t share any data and
		 * communicate between each other with messages. Nested workers are
		 * not supported. An example is available in `tests/test_worker.js`.
		 */
		constructor(module_filename: string);
		/**
		 * Send a message to the corresponding worker. msg is cloned in the
		 * destination worker using an algorithm similar to the HTML structured
		 * clone algorithm. SharedArrayBuffer are shared between workers.
		 *
		 * Current limitations: `Map` and `Set` are not supported yet.
		 */
		postMessage(msg: WorkerMessage): void;
		/**
		 * Getter and setter. Set a function which is called each time a
		 * message is received. The function is called with a single argument.
		 * It is an object with a `data` property containing the received
		 * message. The thread is not terminated if there is at least one non
		 * `null` onmessage handler.
		 */
		onmessage: (msg: WorkerMessage) => void;
	}
}
