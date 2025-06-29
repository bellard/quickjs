/** Command line arguments where argument[0] is the JS script name. */
declare const scriptArgs: string[];
/** Print args separated by spaces and a trailing newline. */
declare function print(...args: any[]): void;

declare const console: {
	/** Print args separated by spaces and a trailing newline. */
	log: typeof print
};

interface ImportMeta {
	url: string;
	main: boolean;
}
