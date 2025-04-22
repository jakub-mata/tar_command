#include <stdio.h>
#include <stdbool.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#define NAME_LENGTH 100
#define MODE_LENGTH 8
#define UID_LENGTH 8
#define GID_LENGTH 8
#define SIZE_LENGTH 12
#define MTIME_LENGTH 12
#define CHKSUM_LENGTH 8
#define LINKNAME_LENGTH 100
#define MAGIC_LENGTH 6
#define VERSION_LENGTH 2
#define UNAME_LENGTH 32
#define GNAME_LENGTH 32
#define DEVMAJOR_LENGTH 8
#define DEVMINOR_LENGTH 8
#define PREFIX_LENGTH 155
#define TRAILING_PADDING 12
#define RECORD_SIZE 512

/* ARGUMENT PARSING */

struct Args { bool is_valid; char* output_file; char** members; int member_count; bool should_list; };

/* An enum of possible flags */
enum ArgOption { None = 0, Filename = 0b1, List = 0b10 };

/**
 * Adds char* member to the members array of struct Args.
 */
void
add_member(struct Args* args, char* member)
{
	args->members[args->member_count] = member;
	args->member_count++;
}

/**
 * Parses a flag from the command line arguments and modifies the provided struct Args.
 * (a flag is an argument starting with '-', this initial dash is skipped before calling this function)
 * Parameters:
 * - args: pointer to the struct Args to modify
 * - flag: the flag to parse (e.g. "farchive.tar", "f", "t", "tfile.txt")
 * - options: pointer to the enum ArgOption to modify
 */
void parse_flag(struct Args* args, char* flag, enum ArgOption* options)
{
	switch (*flag)
	{
        case 'f':
            args->is_valid = true;
			*options |= Filename;
			++flag;
			if (*flag != '\0') /* If the option is followed by a value, set it, e.g. -farchive.tar */
				args->output_file = flag;
			break;
		case 't':
			args->should_list = true;
			if (*options == Filename && args->output_file == NULL)
				errx(2, "-t flag set before defining archive file");
			
			*options |= List;
			++flag;
			if (*flag != '\0')
				add_member(args, flag);
		    break;
		default:
			errx(2, "Unknown option: %s", flag);
	}
}


/**
 * Parses command line arguments and returns a struct Args with the parsed values.
 * The function expects the first argument to be the program name, which is skipped.
 */
struct Args 
parse_arguments(int argc, char** argv)
{
    struct Args parsed = {
		.is_valid = false, 
		.output_file = NULL, 
		.should_list = false,
		.members = NULL,
		.member_count = 0
	};
	/* Preallocate memory for members */
	parsed.members = malloc(sizeof(char*) * (argc-1));
	if (parsed.members == NULL)
		err(2, "malloc");

    enum ArgOption options = None;
	argv++;  /* Skip the program name */
	argc--;  /* Decrease the argument count */
	if (argc == 0)
		errx(2, "No arguments provided");

    for (int i = 0; i < argc; ++i)
    {
        if (argv[i][0] == '-')
		{
	    	parse_flag(&parsed, argv[i] + 1, &options);
			continue;
	    }

		if ((options & Filename) == 0)
			errx(2, "Value provided without option: %s", argv[i]);

		if (parsed.output_file == NULL) {
			parsed.output_file = argv[i];
			parsed.is_valid = true;
		} else if ((options & List) != 0) {  /* If the output file is already set, add it to members */
			add_member(&parsed, argv[i]);
		} else 
			errx(2, "Value provided without option: %s", argv[i]);
	}
	return parsed;
}


/* ACTUAL TAR PROGRAM */

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

/**
 * Reads a fixed number of characters from the file pointer into the buffer.
 */
void
read_char_based(FILE* fp, char* buffer, size_t size)
{
	size_t read = fread(buffer, sizeof(buffer[0]), size, fp);
	buffer[size] = '\0';  /* Null-terminate the string */
	if (read != size) {
		if (feof(fp))
			errx(2, "Unexpected EOF in archive");
		else
			errx(2, "read_char_based");
	}
	return;
}

/**
 * Reads an integer from the file pointer and returns it.
 * The integer is read as a string of octal digits, and then converted to an unsigned long long.
 * If the first bit is set, it is interpreted as a base 256 number.
 */
size_t
read_integer_based(FILE* fp, size_t size)
{
	char buffer[size];
	size_t read = fread(buffer, sizeof(buffer[0]), size, fp);
	if (read != size) {
		if (feof(fp))
			errx(2, "Unexpected EOF in archive");
		else {
			errx(2, "read_integer_based");
		}
	}

	int first_bit_mask = 0b10000000;
	// Files over 8G extension (star(1) extension)
	if ((buffer[0] & first_bit_mask) == first_bit_mask) {
		// represented in base 256
		buffer[0] &= 0b01111111;  // clear the sign bit
		printf("Base 256\n");
		return strtoull(buffer, NULL, 256);
	}
	// represented in octal
	return strtoull(buffer, NULL, 8);
}

char
read_char(FILE* fp)
{
	char c;
	size_t read = fread(&c, sizeof(char), 1, fp);
	if (read != 1) {
		if (feof(fp))
			errx(2, "Unexpected EOF in archive");
		else
			errx(2, "read_typeflag");
	}
	return c;
}

bool
check_EOF(FILE* fp)
{
	char c;
	size_t read = fread(&c, sizeof(char), 1, fp);
	if (feof(fp))
		return true;
	if (read == 0)
		errx(2, "check_EOF");
	if (read == 1) {
		ungetc(c, fp);  // put the character back
		return false;
	}
	errx(2, "check_EOF");
}

/**
 * Reads a tar header from the file pointer.
 * Returns true if the header was read successfully, false if EOF reached at first character.
 */
void
read_header(FILE* fp, struct TarHeader* header, bool* EOF_reached, size_t* record_counter)
{
	if (check_EOF(fp)) {
		#ifdef DEBUG
			printf("Unexpected EOF\n");
		#endif
		*EOF_reached = true;
		return;
	}
	memset(header, 0, sizeof *header);  /* Initialize the header to zero */
	// Read name
	read_char_based(fp, header->name, NAME_LENGTH);
	header->mode = read_integer_based(fp, MODE_LENGTH);
	header->uid = read_integer_based(fp, UID_LENGTH);
	header->gid = read_integer_based(fp, GID_LENGTH);
	header->size = read_integer_based(fp, SIZE_LENGTH);
	header->mtime = read_integer_based(fp, MTIME_LENGTH);
	header->chksum = read_integer_based(fp, CHKSUM_LENGTH);
	header->typeflag = read_char(fp);
	if (header->typeflag != '0' && header->typeflag != '\0') // supporting only regular files
		errx(2, "Unsupported header type: %d", header->typeflag);
	read_char_based(fp, header->linkname, LINKNAME_LENGTH);
	read_char_based(fp, header->magic, MAGIC_LENGTH);
	if (*header->magic != '\0' && strncmp(header->magic, "ustar", MAGIC_LENGTH - 1) != 0)
	{
		#ifdef DEBUG
			printf("Magic: %s\n", header->magic);
		#endif
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

void print_header(struct TarHeader* header)
{
	if (header->prefix[0] != '\0')
		printf("%s/", header->prefix);
	printf("%s\n", header->name);
	#ifdef DEBUG
		printf("Mode: %zu\n", header->mode);
		printf("UID: %zu\n", header->uid);
		printf("GID: %zu\n", header->gid);
		printf("Size: %zu\n", header->size);
		printf("MTIME: %zu\n", header->mtime);
		printf("CHKSUM: %zu\n", header->chksum);
		printf("Typeflag: %c\n", header->typeflag);
		printf("Linkname: %s\n", header->linkname);
		printf("Magic: %s\n", header->magic);
		printf("Version: %s\n", header->version);
		printf("Uname: %s\n", header->uname);
		printf("Gname: %s\n", header->gname);
		printf("Devmajor: %zu\n", header->devmajor);
		printf("Devminor: %zu\n", header->devminor);
		printf("Prefix: %s\n", header->prefix);
	#endif
}

bool is_header_empty(struct TarHeader* header)
{
	if (header->name[0] == '\0' && header->mode == 0 && header->uid == 0 &&
		header->gid == 0 && header->size == 0 && header->mtime == 0 &&
		header->chksum == 0 && header->typeflag == '\0' &&
		header->linkname[0] == '\0' && header->magic[0] == '\0' &&
		header->version[0] == '\0' && header->uname[0] == '\0' &&
		header->gname[0] == '\0' && header->devmajor == 0 &&
		header->devminor == 0 && header->prefix[0] == '\0') {
			return true;
	}
	return false;
}

bool
is_end_of_archive(FILE* fp, struct TarHeader* header, bool* EOF_reached, size_t* record_counter)
{
	if (*EOF_reached)
		return true;
	/* End of archive is defined by the standard as two consecutive empty records (headers) */
	if (is_header_empty(header)) {
		#ifdef DEBUG
			printf("\tFirst record empty\n");
		#endif
		/* The first record is empty, check the next one */
		read_header(fp, header, EOF_reached, record_counter);
		if (*EOF_reached) {
			warnx("A lone zero block at %zu", *record_counter);
			return true;
		}
		if (is_header_empty(header)) {
			#ifdef DEBUG
				printf("\tSecond record empty\n");
			#endif
			/* The second record is also empty, we reached the end of the archive */
			return true;
		}
		#ifdef DEBUG
			printf("\tSecond record NOT empty\n");
		#endif
		/* The second record is not empty */
		/* Print the first archive to the console */
		struct TarHeader empty_header = {0};
		print_header(&empty_header);
		/* The second record not empty and currently set to header */
	}
	return false;
}

void skip_data_records(FILE* fp, size_t size, size_t *record_counter)
{
	size_t data_record_amount = (RECORD_SIZE - 1 + size) / RECORD_SIZE;  /* Defined by standard */
	#ifdef DEBUG
		printf("\tData record amount: %zu\n", data_record_amount);
	#endif

	/* Reads all data blocks except for last character which will be checked for EOF*/
	int ok = fseek(fp, RECORD_SIZE * data_record_amount - 1, SEEK_CUR); 
	if (ok != 0)
		errx(2, "fseek");
	char test_c = fgetc(fp);
	if (test_c == EOF) {
		warnx("Unexpected EOF in archive");
		errx(2, "Error is not recoverable: exiting now");
	}
	
	*record_counter += data_record_amount;
}

void
print_members(struct Args* args, bool* is_present)
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
			errx(2, "Exiting with failure status due to previous errors");
	}
}

void
list_archive(FILE* fp, struct Args* args)
{	
	rewind(fp);
	struct TarHeader read;
	/* Initialize array showing if the member is present */
	bool* is_present = malloc(sizeof(bool) * args->member_count);
	if (is_present == NULL && args->member_count > 0)
		err(2, "malloc");
	for (int i = 0; i < args->member_count; ++i)
		is_present[i] = false;
	
	bool EOF_reached = false;
	size_t record_counter = 0;
	while (read_header(fp, &read, &EOF_reached, &record_counter), !is_end_of_archive(fp, &read, &EOF_reached, &record_counter))
	{
		#ifdef DEBUG
			printf("**********\n");
		#endif
		bool should_print = (args->member_count == 0);
		for (int i = 0; i < args->member_count; ++i) {
			if (strcmp(read.name, args->members[i]) == 0) {
				should_print = true;
				is_present[i] = true;
				break;
			}
		}
		if (should_print)
			print_header(&read);

		skip_data_records(fp, read.size, &record_counter);
	}

	#ifdef DEBUG
		printf("**********\n");
		printf("End of archive\n");
	#endif
	print_members(args, is_present);
	free(is_present);
}

int
main(int argc, char* argv[])
{
    struct Args args = parse_arguments(argc, argv);
    if (!args.is_valid)
        errx(2, "%s", "Provided arguments are not valid");

	#ifdef DEBUG
		printf("Output file: %s\n", args.output_file);
		printf("Should list: %s\n", args.should_list ? "true" : "false");
		for (int i = 0; i < args.member_count; ++i)
			printf("Member %d: %s\n", i, args.members[i]);
		printf("Member count: %d\n", args.member_count);
		printf("**********\n");
	#endif

    FILE* fp;
	if ((fp = fopen(args.output_file, "r")) == NULL)
		err(2, "fopen");

	if (args.should_list)
		list_archive(fp, &args);

	free(args.members);
	fclose(fp);
}
