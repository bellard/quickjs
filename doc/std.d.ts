/**
 * The std module provides wrappers to the libc stdlib.h and stdio.h and a
 * few other utilities.
 */
declare module "std" {
	import { FileDescriptor, ExitStatus, Errno } from "os";

	/**
	 * FILE prototype
	 */
	export interface FILE {
		/**
		 * Close the file. Return 0 if OK or `-errno` in case of I/O error.
		 */
		close(): number
		/**
		 * Outputs the string with the UTF-8 encoding.
		 */
		puts(str: string): void;
		/**
		 * Formatted printf.
		 *
		 * The same formats as the standard C library printf are supported.
		 * Integer format types (e.g. `%d`) truncate the Numbers or BigInts to 32
		 * bits. Use the `l` modifier (e.g. `%ld`) to truncate to 64 bits.
		 */
		printf(format: string, ...args: any[]): number;
		/**
		 * Flush the buffered file.
		 */
		flush(): void;
		/**
		 * Seek to a give file position (whence is `std.SEEK_*`). `offset` can
		 * be a number or a bigint. Return 0 if OK or `-errno` in case of I/O
		 * error.
		 */
		seek(offset: number, whence: number): number;
		/**
		 * Return the current file position.
		 */
		tell(): number;
		/**
		 * Return the current file position as a bigint.
		 */
		tello(): bigint;
		/**
		 * Return true if end of file.
		 */
		eof(): boolean;
		/**
		 * Return the associated OS handle.
		 */
		fileno(): FileDescriptor;
		/**
		 * Return true if there was an error.
		 */
		error(): boolean;
		/**
		 * Clear the error indication.
		 */
		clearerr(): void;
		/**
		 * Read `length` bytes from the file to the ArrayBuffer `buffer` at
		 * byte position `position` (wrapper to the libc `fread`).
		 */
		read(buffer: ArrayBuffer, position: number, length: number): number;
		/**
		 * Write `length` bytes to the file from the ArrayBuffer `buffer` at
		 * byte position position (wrapper to the libc `fwrite`).
		 */
		write(buffer: ArrayBuffer, postion: number, length: number): number;
		/**
		 * Return the next line from the file, assuming UTF-8 encoding, excluding
		 * the trailing line feed.
		 */
		getline(): string;
		/**
		 * Read `max_size` bytes from the file and return them as a string
		 * assuming UTF-8 encoding. If `max_size` is not present, the file is
		 * read up its end.
		 */
		readAsString(max_size?: number): string;
		/**
		 * Return the next byte from the file. Return -1 if the end of file is
		 * reached.
		 */
		getByte(): number
		/**
		 * Write one byte to the file.
		 */
		putByte(c: number): number;
	}

	export interface EvalOptions {
		/**
		 * Boolean (default = `false`). If `true`, error backtraces do not list
		 * the stack frames below the evalScript.
		 */
		backtrace_barrier?: boolean;
	}

	export interface ErrorObj {
		errno?: number;
	}

	export interface UrlGetOptions {
		/**
		 * Boolean (default = `false`). If `true`, the response is an
		 * ArrayBuffer instead of a string. When a string is returned, the
		 * data is assumed to be UTF-8 encoded.
		 */
		binary?: boolean;
		/**
		* Boolean (default = `false`). If `true`, return the an object contains
		* the properties response (response content), responseHeaders (headers
		* separated by CRLF), status (status code). response is null is case of
		* protocol or network error. If full is false, only the response is
		* returned if the status is between 200 and 299. Otherwise null is
		* returned.
		*/
		full?: boolean;
	}

	export interface UrlGetResponse<T> {
		response: T | null;
		status: number;
		responseHeaders: string;
	}

	/**
	 * Result that either represents a FILE or null on error.
	 */
	export type FILEResult = FILE | null;

	/**
	 * Exit the process.
	 */
	export function exit(n: ExitStatus): never;
	/**
	  * Evaluate the string `str` as a script (global eval).
	  */
	export function evalScript(str: string, options?: EvalOptions): any;
	/**
	 * Evaluate the file filename as a script (global eval).
	 */
	export function loadScript(filename: string): any;
	/**
	 * Load the file filename and return it as a string assuming UTF-8
	 * encoding. Return `null` in case of I/O error.
	 */
	export function loadFile(filename: string): string | null;
	/**
	 * Open a file (wrapper to the libc fopen()). Return the FILE object or
	 * `null` in case of I/O error. If errorObj is not undefined, set its
	 * `errno` property to the error code or to 0 if no error occured.
	 */
	export function open(filename: string, flags: string, errorObj?: ErrorObj): FILEResult;
	/**
	 * Open a process by creating a pipe (wrapper to the libc `popen()`).
	 * Return the `FILE` object or `null` in case of I/O error. If `errorObj`
	 * is not `undefined`, set its `errno` property to the error code or to 0
	 * if no error occured.
	 */
	export function popen(command: string, flags: string, errorObj?: ErrorObj): FILEResult;
	/**
	 * Open a file from a file handle (wrapper to the libc `fdopen()`). Return
	 * the `FILE` object or `null` in case of I/O error. If `errorObj` is not
	 * `undefined`, set its errno property to the error code or to 0 if no
	 * error occured.
	 */
	export function fdopen(fd: FileDescriptor, flags: string, errorObj?: ErrorObj): FILEResult;
	/**
	 * Open a temporary file. Return the `FILE` object or `null` in case of I/O
	 * error. If `errorObj` is not undefined, set its `errno` property to the
	 * error code or to 0 if no error occured.
	 */
	export function tmpfile(errorObj?: ErrorObj): FILE;
	/**
	 * Equivalent to `std.out.puts(str)`.
	 */
	export const puts: typeof out.puts;
	/**
	 * Equivalent to `std.out.printf(fmt, ...args)`.
	 */
	export const printf: typeof out.printf;
	/**
	 * Equivalent to the libc `sprintf()`.
	 */
	export function sprintf(format: string, ...args: any[]): string;
	const $in: FILE;
	/**
	 * Wrappers to the libc file `stdin`, `stdout`, `stderr`.
	 */
	export { $in as in };
	/**
	 * Wrappers to the libc file `stdin`, `stdout`, `stderr`.
	 */
	export const out: FILE;
	/**
	 * Wrappers to the libc file `stdin`, `stdout`, `stderr`.
	 */
	export const err: FILE;
	/**
	 * Constants for seek().
	 */
	export const SEEK_CUR: number;
	/**
	 * Constants for seek().
	 */
	export const SEEK_END: number;
	/**
	 * Constants for seek().
	 */
	export const SEEK_SET: number;
	/**
	 * Enumeration object containing the integer value of common errors
	 * (additional error codes may be defined):
	 */
	export const Error: {
		readonly EACCES: number,
		readonly ENOENT: number,
		readonly EBADF: number,
		readonly ENOSPC: number,
		readonly EBUSY: number,
		readonly ENOSYS: number,
		readonly EEXIST: number,
		readonly EPERM: number,
		readonly EINVAL: number,
		readonly EPIPE: number,
		readonly EIO: number,
		readonly EAGAIN: number,
		readonly EINPROGRESS: number,
		readonly EWOULDBLOCK: number,
	};
	/**
	 * Return a string that describes the error `errno`.
	 */
	export function strerror(errno: Errno): string;
	/**
	 * Manually invoke the cycle removal algorithm. The cycle removal
	 * algorithm is automatically started when needed, so this function is
	 * useful in case of specific memory constraints or for testing.
	 */
	export function gc(): void;
	/**
	 * Return the value of the environment variable `name` or `undefined` if it
	 * is not defined.
	 */
	export function getenv(name: string): string | undefined;
	/**
	 * Set the value of the environment variable `name` to the string `value`.
	 */
	export function setenv(name: string, value: string): void;
	/**
	 * Delete the environment variable `name`.
	 */
	export function unsetenv(name: string): void;
	/**
	 * Return an object containing the environment variables as key-value pairs.
	 */
	export function getenviron(): { [key: string]: string };
	/** Download url using the curl command line utility. */
	export function urlGet(url: string, options?: UrlGetOptions): string | null;
	export function urlGet(url: string, options: UrlGetOptions & { full?: false, binary: true }): ArrayBuffer | null;
	export function urlGet(url: string, options: UrlGetOptions & { full: true, binary?: false }): UrlGetResponse<string>;
	export function urlGet(url: string, options: UrlGetOptions & { full: true, binary: true }): UrlGetResponse<ArrayBuffer>;
	/**
	 * Parse `str` using a superset of `JSON.parse`. The following extensions are accepted:
	 *
	 * - Single line and multiline comments
	 * - unquoted properties (ASCII-only Javascript identifiers)
	 * - trailing comma in array and object definitions
	 * - single quoted strings
	 * - \\f and \\v are accepted as space characters
	 * - leading plus in numbers
	 * - octal (0o prefix) and hexadecimal (0x prefix) numbers
	 */
	export function parseExtJSON(str: string): any;
}
