#include <stdio.h>
#include <stdbool.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#define	NAME_LENGTH 100
#define	MODE_LENGTH 8
#define	UID_LENGTH 8
#define	GID_LENGTH 8
#define	SIZE_LENGTH 12
#define	MTIME_LENGTH 12
#define	CHKSUM_LENGTH 8
#define	LINKNAME_LENGTH 100
#define	MAGIC_LENGTH 6
#define	VERSION_LENGTH 2
#define	UNAME_LENGTH 32
#define	GNAME_LENGTH 32
#define	DEVMAJOR_LENGTH 8
#define	DEVMINOR_LENGTH 8
#define	PREFIX_LENGTH 155
#define	TRAILING_PADDING 12
#define	RECORD_SIZE 512

#define	MAGIC "ustar"

/*
 * ARGUMENT STRUCTURES
 */

struct Args {
	char *output_file;
	char **members;
	int member_count;
	bool should_list;
	bool should_extract;
	bool is_verbose;
};

enum ArgOption {
	None = 0,			// No option provided
    Filename = 0b1,		// Output tar archive file name specified
	List = 0b10,		// List contents of the archive
    Extract = 0b100		// Extract files from the archive
};


/*
 * ARGUMENT UTILITIES
 */

/* Adds a file member to the args structure */
void
add_member(struct Args *args, char *member) {
    if (!args || !member) {
		fprintf(stderr, "Error: Null pointer passed to add_member.\n");
		return;
	}
    args->members[args->member_count] = member;
    args->member_count++;
}

/* Parse a single flag and update args and options accordingly */
void
parse_flag(struct Args *args, char *flag, enum ArgOption *options)
{
	switch (*flag) {
		case 'f': *options |= Filename; break;
		case 't': args->should_list = true; *options |= List; break;
		case 'x': args->should_extract = true; *options |= Extract; break;
		case 'v': args->is_verbose = true; break;
		default: errx(2, "Unknown option: %c", *flag);
	}
}

void
validate_args(struct Args *args, enum ArgOption options)
{
	if (args == NULL) {
		errx(2, "Null pointer passed to validate_args.\n");
		return;
	}
	if ((options & Filename) == 0)
		errx(2, "No output file specified");
	if ((options & (List | Extract)) == 0)
		errx(2, "No action specified: -t or -x");
	if (args->should_list && args->should_extract)
		errx(2, "Cannot list and extract at the same time");
}

/*
 * Parses cl arguments and returns a struct Args with the parsed values.
 * The function expects the first argument to be the program name.
 */
struct Args
parse_arguments(int argc, char **argv)
{
	struct Args parsed = {
		.output_file = NULL,
		.should_list = false,
		.members = NULL,
		.member_count = 0,
		.is_verbose = false,
		.should_extract = false
	};
	/* Preallocate memory for members */
	parsed.members = malloc(sizeof (char *) * (argc - 1));
	if (parsed.members == NULL)
		err(2, "malloc");

	enum ArgOption options = None;
	argv++;  /* Skip the program name */
	argc--;  /* Decrease the argument count */
	if (argc == 0)
		errx(2, "No arguments provided");

	for (int i = 0; i < argc; ++i) {
		if (argv[i][0] == '-') {
			parse_flag(&parsed, argv[i] + 1, &options);
			continue;
	    }

		if ((options & Filename) == 0)
			errx(2, "Value provided without option: %s", argv[i]);

		if (parsed.output_file == NULL)
			parsed.output_file = argv[i];
		else
			add_member(&parsed, argv[i]);
	}
	validate_args(&parsed, options);
	return (parsed);
}


/*
 * MAIN TAR LOGIC
 */


struct TarHeader
{
	char name[NAME_LENGTH + 1];
	size_t mode;
	size_t uid;
	size_t gid;
	size_t size;
	size_t mtime;
	size_t chksum;
	char typeflag;
	char linkname[LINKNAME_LENGTH + 1];
	char magic[MAGIC_LENGTH + 1];
	char version[VERSION_LENGTH + 1];
	char uname[UNAME_LENGTH + 1];
	char gname[GNAME_LENGTH + 1];
	size_t devmajor;
	size_t devminor;
	char prefix[PREFIX_LENGTH + 1];
};

/* Reads a fixed number of characters from the file pointer into the buffer. */
void
read_char_based(FILE *fp, char *buffer, size_t size)
{
	size_t read = fread(buffer, sizeof (char), size, fp);
	buffer[size] = '\0';  /* Null-terminate the string */
	if (read != size) {
		if (feof(fp))
			errx(2, "Unexpected EOF in archive");
		else
			errx(2, "Error reading character");
	}
}

/*
 * Converts a string of octal digits to an unsigned long long integer.
 * The first bit is used to determine if the number is in base 256.
 */
size_t
to_base_256(char *buffer, size_t buffer_size)
{
	size_t result = 0;
	for (size_t i = 1; i < buffer_size; ++i) {
		result = (result << 8) | (unsigned char)buffer[i];
	}
	return (result);
}

/*
 * Reads an integer from the file pointer and returns it. The integer is read
 * as a string of octal digits, and then converted to anunsigned long long.
 * If the first bit is set, it is interpreted as a base 256 number.
 */
size_t
read_integer_based(FILE *fp, size_t size)
{
	char buffer[size];
	size_t read = fread(buffer, sizeof (buffer[0]), size, fp);
	if (read != size) {
		if (feof(fp))
			errx(2, "Unexpected EOF in archive");
		else {
			errx(2, "Error reading integer");
		}
	}

	int first_bit_mask = 0b10000000;
	// Files over 8G extension (star(1) extension)
	if ((buffer[0] & first_bit_mask) == first_bit_mask) {
		return (to_base_256(buffer, size));
	}
	// represented in octal
	return (strtoull(buffer, NULL, 8));
}

char
read_char(FILE *fp)
{
	char c;
	size_t read = fread(&c, sizeof (char), 1, fp);
	if (read != 1) {
		if (feof(fp))
			errx(2, "Unexpected EOF in archive");
		else
			errx(2, "Error reading character");
	}
	return (c);
}

/*
 * Checks if the end of the file has been reached. Returns true if EOF is
 * reached, false otherwise.
 */
bool
check_EOF(FILE *fp)
{
	char c;
	size_t read = fread(&c, sizeof (char), 1, fp);
	if (feof(fp))
		return (true);
	if (read == 0)
		errx(2, "Error in check_EOF other than EOF");
	if (read == 1) {
		ungetc(c, fp);  // put the character back
		return (false);
	}
	errx(2, "Error in check_EOF other than EOF");
}

/*
 * Reads a tar header from the file pointer. The header is read into the
 * provided TarHeader structure. The function also updates the EOF_reached
 * and record_counter variables.
 */
void
read_header(
	FILE *fp,
	struct TarHeader *header,
	bool *EOF_reached,
	size_t *record_counter)
{
	if (check_EOF(fp)) {
		*EOF_reached = true;
		return;
	}
	memset(header, 0, sizeof (*header));  /* Initialize header to zero */
	read_char_based(fp, header->name, NAME_LENGTH);
	header->mode = read_integer_based(fp, MODE_LENGTH);
	header->uid = read_integer_based(fp, UID_LENGTH);
	header->gid = read_integer_based(fp, GID_LENGTH);
	header->size = read_integer_based(fp, SIZE_LENGTH);
	header->mtime = read_integer_based(fp, MTIME_LENGTH);
	header->chksum = read_integer_based(fp, CHKSUM_LENGTH);
	header->typeflag = read_char(fp);
	if (header->typeflag != '0' && header->typeflag != '\0') // non-regular
		errx(2, "Unsupported header type: %d", header->typeflag);
	read_char_based(fp, header->linkname, LINKNAME_LENGTH);
	read_char_based(fp, header->magic, MAGIC_LENGTH);
	if (*header->magic != '\0'
		&& strncmp(header->magic, MAGIC, MAGIC_LENGTH - 1) != 0) {
		warnx("This does not look like a tar archive");
		errx(2, "Exiting with failure status due to previous errors");
	}
	read_char_based(fp, header->version, VERSION_LENGTH);
	read_char_based(fp, header->uname, UNAME_LENGTH);
	read_char_based(fp, header->gname, GNAME_LENGTH);
	header->devmajor = read_integer_based(fp, DEVMAJOR_LENGTH);
	header->devminor = read_integer_based(fp, DEVMINOR_LENGTH);
	read_char_based(fp, header->prefix, PREFIX_LENGTH);

	/* Skip traling header */
	char void_buffer[TRAILING_PADDING];  /* Will be thrown away */
	read_char_based(fp, void_buffer, TRAILING_PADDING);

	*record_counter += 1;
}

void
print_header(struct TarHeader *header)
{
	if (header->prefix[0] != '\0')
		printf("%s/", header->prefix);
	printf("%s\n", header->name);
}

bool
is_header_empty(struct TarHeader *header)
{
	if (header->name[0] == '\0' && header->mode == 0 && header->uid == 0 &&
		header->gid == 0 && header->size == 0 && header->mtime == 0 &&
		header->chksum == 0 && header->typeflag == '\0' &&
		header->linkname[0] == '\0' && header->magic[0] == '\0' &&
		header->version[0] == '\0' && header->uname[0] == '\0' &&
		header->gname[0] == '\0' && header->devmajor == 0 &&
		header->devminor == 0 && header->prefix[0] == '\0') {
			return (true);
	}
	return (false);
}

/*
 * Checks if the end of the archive has been reached. The end of the archive
 * is defined as two consecutive empty records (headers). If EOF is reached,
 * it sets EOF_reached to true and returns true. Otherwise, it returns false.
 */
bool
is_end_of_archive(
	FILE *fp,
	struct TarHeader *header,
	bool *EOF_reached,
	size_t *record_counter)
{
	if (*EOF_reached)
		return (true);
	if (is_header_empty(header)) {
		/* The first record is empty, check the next one */
		read_header(fp, header, EOF_reached, record_counter);
		if (*EOF_reached) {
			warnx("A lone zero block at %zu", *record_counter);
			return (true);
		}
		if (is_header_empty(header)) {
			/* Two consecutive empty record => end of the archive */
			return (true);
		}
		/* The second record is not empty */
		/* Print the first archive to the console */
		struct TarHeader empty_header = {0};
		print_header(&empty_header);
		/* The second record not empty and currently set to header */
	}
	return (false);
}

/* Skips the data records in the tar archive. */
void
skip_data_records(FILE *fp, size_t size, size_t *record_counter)
{
	size_t data_record_amount = (RECORD_SIZE - 1 + size) / RECORD_SIZE;
	/* Reads all data blocks except for last character */
	int ok = fseek(fp, RECORD_SIZE * data_record_amount - 1, SEEK_CUR);
	if (ok != 0)
		errx(2, "Error seeking in archive");
	/* Last char checked for EOF */
	char test_c = fgetc(fp);
	if (test_c == EOF) {
		warnx("Unexpected EOF in archive");
		errx(2, "Error is not recoverable: exiting now");
	}

	*record_counter += data_record_amount;
}

/*
 * Prints the names of the members that were not found in the archive.
 * If any member is missing, it exits with a failure status.
 */
void
print_missing_members(struct Args *args, bool *is_present)
{
	bool missing_found = false;
	if (args->member_count > 0) {
		for (int i = 0; i < args->member_count; ++i) {
			if (!is_present[i]) {
				missing_found = true;
				warnx("%s: Not found in archive", args->members[i]);
			}
		}
		if (missing_found)
			errx(
				2, "Exiting with failure status due to previous errors");
	}
}

/*
 * Reads a data record from the input file pointer and writes it to the
 * output file pointer. The amount parameter specifies the number of bytes to
 * write - important for the last record, which may be less than RECORD_SIZE.
 */
void
data_rw(FILE *input_fp, FILE *output_fp, char *buffer, size_t amount)
{
	size_t r = fread(buffer, sizeof (char), RECORD_SIZE, input_fp);
	if (r != RECORD_SIZE) {
		if (feof(input_fp)) {
			warnx("Unexpected EOF in archive");
			fwrite(buffer, r, 1, output_fp);  // write what was read
			fflush(output_fp);
			errx(2, "Error is not recoverable: exiting now");
		}
		else
			err(2, "Error reading data record");
	}
	size_t w = fwrite(buffer, amount, 1, output_fp);
	if (w != 1) {
		fflush(output_fp);
		err(2, "Error writing to output file");
	}
}

void
extract_file(FILE *fp, struct TarHeader *header, size_t *record_counter)
{
	char full_name[NAME_LENGTH + PREFIX_LENGTH + 2] = {0};
	if (header->prefix[0] != '\0') {
		snprintf(
			full_name,
			sizeof (full_name),
			"%s/%s",
			header->prefix,
			header->name);
	} else {
		snprintf(
			full_name,
			sizeof (full_name),
			"%s",
			header->name);
	}

	FILE *extracted_fp = fopen(full_name, "w");
	if (extracted_fp == NULL)
		err(2, "Error opening file for writing: %s", full_name);
	size_t data_record_amount = (RECORD_SIZE - 1 + header->size) / RECORD_SIZE;
	size_t remainder = header->size % RECORD_SIZE;
	if (remainder == 0)
		remainder = RECORD_SIZE;
	char *buffer = malloc(RECORD_SIZE);
	for (size_t i = 0; i < data_record_amount; ++i) {
		*record_counter += 1;
		if (i < data_record_amount - 1)
			data_rw(fp, extracted_fp, buffer, RECORD_SIZE);
		else
			data_rw(fp, extracted_fp, buffer, remainder);
	}
	free(buffer);
	fclose(extracted_fp);
}

/*
 * Checks if the member file should be handled based on the command line
 * arguments. It also updates the is_present array accordingly.
 */
bool
should_handle_member(
	struct Args *args,
	struct TarHeader *header,
	bool* is_present)
{
	if (args->member_count == 0)
		return (true);
	for (int i = 0; i < args->member_count; ++i) {
		if (strcmp(header->name, args->members[i]) == 0) {
			*(is_present + i) = true;
			return (true);
		}
	}
	return (false);
}

/*
 * Traverses the tar archive and processes each member. If the member is
 * present in the archive, it is either listed or extracted, depending on the
 * command line arguments. If the member is not present, a warning is printed.
 */
void
traverse_archive(FILE *fp, struct Args *args)
{
	rewind(fp);
	struct TarHeader read;
	/* Initialize array showing whether the member is present */
	bool* is_present = args->member_count > 0
		? malloc(sizeof (bool) * args->member_count)
		: NULL;
	if (is_present == NULL && args->member_count > 0)
		err(2, "Error allocating memory");
	for (int i = 0; i < args->member_count; ++i)
		is_present[i] = false;

	bool EOF_reached = false;
	size_t record_counter = 0;
	while (
		read_header(fp, &read, &EOF_reached, &record_counter),
		!is_end_of_archive(fp, &read, &EOF_reached, &record_counter)) {
		bool should_handle = should_handle_member(args, &read, is_present);

		if (args->should_list) {
			if (should_handle)
				print_header(&read);
			skip_data_records(fp, read.size, &record_counter);
		} else if (args->should_extract) {
			if (should_handle) {
				if (args->is_verbose)
					print_header(&read);
				extract_file(fp, &read, &record_counter);
			} else {
				skip_data_records(fp, read.size, &record_counter);
			}
		}
	}
	print_missing_members(args, is_present);
	if (args->member_count > 0)
		free(is_present);
}

int
main(int argc, char *argv[])
{
	struct Args args = parse_arguments(argc, argv);
	FILE *fp;
	if ((fp = fopen(args.output_file, "r")) == NULL)
		err(2, "Error opening file: %s", args.output_file);

	if (args.should_list || args.should_extract)
		traverse_archive(fp, &args);

	free(args.members);
	fclose(fp);
}
