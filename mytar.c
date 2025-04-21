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

#define DEBUG true

/* Argument parsing */
/*
Currently supports passing options and values separately.
For example:
	-f mytar
	-t
	-f mytar -t
	-t -f mytar
	-f mytar -t -f mytar2
*/
struct Args { bool is_valid; char* output_file; char** members; int member_count; bool should_list; };

enum ArgOption { None = 0, List = 0b1, Members = 0b10 };

void
add_member(struct Args* args, char* member)
{
	args->members[args->member_count] = member;
	args->member_count++;
}

void parse_flag(struct Args* args, char* flag, enum ArgOption* options)
{
	switch (*flag)
	{
        case 'f':
		    args->should_list = true;
            args->is_valid = true;
			*options |= List;
			++flag;  // Move to the next character
			if (*flag != '\0') { // If the option is followed by a value, set it
				args->output_file = flag;
			}
			break;
		case 't':
			if (*options == None) {
				errx(2, "-f flag set before defining flag");
			}
			*options |= Members;
			++flag;  // Move to the next character
			if (*flag != '\0') {
				// If the option is followed by a value, add it to members
				add_member(args, flag);
			}
		    break;
		default:
			errx(2, "Unknown option: %s", flag);
	}
}

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
	// Preallocate memory for members
	parsed.members = malloc(sizeof(char*) * (argc-1));
	if (parsed.members == NULL)
		err(2, "malloc");

    enum ArgOption options = None;
	argv++;  // Skip the program name
	argc--;  // Decrease the argument count
	if (argc == 0)
		errx(2, "No arguments provided");

    for (int i = 0; i < argc; ++i)
    {
        if (argv[i][0] == '-')
		{
	    	parse_flag(&parsed, argv[i] + 1, &options);
			continue;
	    }

		if ((options & List) == 0)
			errx(2, "Value provided without option: %s", argv[i]);

		if (parsed.output_file == NULL) {
			// If the output file is not set, set it to the current argument
			parsed.output_file = argv[i];
			parsed.is_valid = true;
		} else if ((options & Members) != 0) {
			// If the output file is already set, add it to members
			add_member(&parsed, argv[i]);
		} else {
			errx(2, "Value provided without option: %s", argv[i]);
		}
	}
	return parsed;
}

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

bool
read_char_based(FILE* fp, char* buffer, size_t size)
{
	size_t read = fread(buffer, sizeof(buffer[0]), size, fp);
	if (read != size)
		return false;
	return true;
}

size_t
read_integer_based(FILE* fp, size_t size, bool* ok)
{
	char buffer[size];
	size_t read = fread(buffer, sizeof(buffer[0]), size, fp);
	if (read != size) {
		*ok = false;
		return 0ULL;
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
	if (read != 1)
		err(2, "read_char");
	return c;
}

void
read_header(FILE* fp, struct TarHeader* header)
{
	bool ok = true;
	// Read name
	if (!read_char_based(fp, header->name, NAME_LENGTH))
		err(2, "read_name");
	// Read mode
	header->mode = read_integer_based(fp, MODE_LENGTH, &ok);
	if (!ok)
		err(2, "read_mode");
	// Read uid
	header->uid = read_integer_based(fp, UID_LENGTH, &ok);
	if (!ok)
		err(2, "read_uid");
	// Read gid
	header->gid = read_integer_based(fp, GID_LENGTH, &ok);
	if (!ok)
		err(2, "read_gid");
	// Read size
	header->size = read_integer_based(fp, SIZE_LENGTH, &ok);
	if (!ok)
		err(2, "read_size");
	// Read mtime
	header->mtime = read_integer_based(fp, MTIME_LENGTH, &ok);
	if (!ok)
		err(2, "read_mtime");
	// Read chksum
	header->chksum = read_integer_based(fp, CHKSUM_LENGTH, &ok);
	if (!ok)
		err(2, "read_chksum");
	// Read typeflag
	header->typeflag = read_char(fp);
	if (header->typeflag != '0' && header->typeflag != '\0') // supporting only regular files
		errx(2, "Unsupported header type: %d", header->typeflag);
	// Read linkname
	if (!read_char_based(fp, header->linkname, LINKNAME_LENGTH))
		err(2, "read_linkname");
	header->linkname[LINKNAME_LENGTH] = '\0';
	// Read magic
	if (!read_char_based(fp, header->magic, MAGIC_LENGTH))
		err(2, "read_magic");
	header->magic[MAGIC_LENGTH] = '\0';
	// Read version
	if (!read_char_based(fp, header->version, VERSION_LENGTH))
		err(2, "read_version");
	header->version[VERSION_LENGTH] = '\0';
	// Read uname
	if (!read_char_based(fp, header->uname, UNAME_LENGTH))
		err(2, "read_uname");
	header->uname[UNAME_LENGTH] = '\0';
	// Read gname
	if (!read_char_based(fp, header->gname, GNAME_LENGTH))
		err(2, "read_gname");
	header->gname[GNAME_LENGTH] = '\0';
	// Read devmajor
	header->devmajor = read_integer_based(fp, DEVMAJOR_LENGTH, &ok);
	if (!ok)
		err(2, "read_devmajor");
	// Read devminor
	header->devminor = read_integer_based(fp, DEVMINOR_LENGTH, &ok);
	if (!ok)
		err(2, "read_devminor");
	// Read prefix
	if (!read_char_based(fp, header->prefix, PREFIX_LENGTH))
		err(2, "read_prefix");
	header->prefix[PREFIX_LENGTH] = '\0';

	// Skip traling header
	char void_buffer[TRAILING_PADDING];  // will be thrown away
	if (!read_char_based(fp, void_buffer, TRAILING_PADDING))
		err(2, "read_trailing_header");
}

void print_header(struct TarHeader* header)
{
	if (header->prefix[0] != '\0') {
		printf("%s/", header->prefix);
	}
	printf("%s\n", header->name);
	if (DEBUG) {
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
	}
}

bool is_header_empty(struct TarHeader* header)
{
	// Check if the header is empty
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
is_end_of_archive(FILE* fp, struct TarHeader* header)
{
	// End of archive is defined by the standard as two consecutive empty records (headers)
	if (is_header_empty(header)) {
		if (DEBUG) {
			printf("\tFirst record empty\n");
		}
		// The first record is empty, check the next one
		read_header(fp, header);
		if (is_header_empty(header)) {
			if (DEBUG) {
				printf("\tSecond record empty\n");
			}
			// The second record is also empty, we reached the end of the archive
			return true;
		}
		// The second record is not empty
		// Print the first archive to the console
		if (DEBUG) {
			printf("\tSecond record NOT empty\n");
		}
		struct TarHeader empty_header = {0};
		print_header(&empty_header);
		// The second record not empty and currently set to header
	}
	return false;
}

void
list_archive(FILE* fp, struct Args* args)
{
	fseek(fp, 0, SEEK_SET);
	struct TarHeader read;
	
	while (read_header(fp, &read), !is_end_of_archive(fp, &read))
	{
		if (DEBUG) {
			printf("**********\n");
		}

		bool should_print = (args->member_count == 0);
		for (int i = 0; i < args->member_count; ++i) {
			if (strcmp(read.name, args->members[i]) == 0) {
				should_print = true;
				break;
			}
		}
		if (should_print) {
			print_header(&read);
		}

		// Skip file data and advance to the next header
		size_t data_record_amount = (RECORD_SIZE - 1 + read.size) / RECORD_SIZE;  // defined by standard
		if (DEBUG) {
			printf("\tData record amount: %zu\n", data_record_amount);
		}
		fseek(fp, RECORD_SIZE * data_record_amount, SEEK_CUR);
	}

	if (DEBUG) {
		printf("**********\n");
		printf("End of archive\n");
	}
}

int
main(int argc, char* argv[])
{
    struct Args args = parse_arguments(argc, argv);
    if (!args.is_valid) {
        errx(2, "%s", "Provided arguments are not valid");
		return 1;
	}

	if (DEBUG) {
		printf("Output file: %s\n", args.output_file);
		printf("Should list: %s\n", args.should_list ? "true" : "false");
		for (int i = 0; i < args.member_count; ++i) {
			printf("Member %d: %s\n", i, args.members[i]);
		}
		printf("Member count: %d\n", args.member_count);
		printf("**********\n");
	}

    FILE* fp;
	if ((fp = fopen(args.output_file, "r")) == NULL)
		err(2, "fopen");

	if (args.should_list)
		list_archive(fp, &args);
}
