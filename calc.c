// Aykhan Asadov 251ADC004
// https://github.com/Aykhanese/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: ChatGPT for parser/CLI debugging and explanation; assignment specification. AI conversation: https://chatgpt.com/share/6ade30e0-39a4-83ed-a98c-9befce0575e7

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

typedef struct
{
    int is_float;
    long long i;
    double d;
} Value;

typedef struct
{
    const char *text;
    size_t pos;
    size_t base_pos;
    size_t error_pos;
    int error;
} Parser;

static Value make_int(long long x)
{
    Value v;
    v.is_float = 0;
    v.i = x;
    v.d = (double)x;
    return v;
}

static Value make_float(double x)
{
    Value v;
    v.is_float = 1;
    v.i = 0;
    v.d = x;
    return v;
}

static double value_as_double(Value v)
{
    return v.is_float ? v.d : (double)v.i;
}

static void skip_spaces(Parser *p)
{
    while (p->text[p->pos] == ' ' || p->text[p->pos] == '\t')
    {
        p->pos++;
    }
}

static Value parse_expression(Parser *p);

static Value parse_number(Parser *p)
{
    size_t start = p->pos;

    while (isdigit((unsigned char)p->text[p->pos]))
    {
        p->pos++;
    }

    if (p->text[p->pos] == '.')
    {
        size_t dot_pos = p->pos;
        p->pos++;

        if (!isdigit((unsigned char)p->text[p->pos]))
        {
            p->error = 1;
            p->error_pos = p->base_pos + dot_pos;
            return make_int(0);
        }

        while (isdigit((unsigned char)p->text[p->pos]))
        {
            p->pos++;
        }

        size_t len = p->pos - start;
        char *tmp = malloc(len + 1);

        if (tmp == NULL)
        {
            p->error = 1;
            p->error_pos = p->base_pos + start;
            return make_int(0);
        }

        memcpy(tmp, p->text + start, len);
        tmp[len] = '\0';

        errno = 0;
        double value = strtod(tmp, NULL);
        free(tmp);

        if (errno == ERANGE || !isfinite(value))
        {
            p->error = 1;
            p->error_pos = p->base_pos + start;
            return make_int(0);
        }

        return make_float(value);
    }

    errno = 0;
    char *end;
    long long value = strtoll(p->text + start, &end, 10);

    if (errno == ERANGE)
    {
        p->error = 1;
        p->error_pos = p->base_pos + start;
        return make_int(0);
    }

    p->pos = (size_t)(end - p->text);
    return make_int(value);
}

static Value parse_primary(Parser *p)
{
    skip_spaces(p);

    char c = p->text[p->pos];

    if (isdigit((unsigned char)c))
    {
        return parse_number(p);
    }

    if (c == '(')
    {
        p->pos++;

        Value value = parse_expression(p);

        if (p->error)
        {
            return make_int(0);
        }

        skip_spaces(p);

        if (p->text[p->pos] != ')')
        {
            p->error = 1;
            p->error_pos = p->base_pos + p->pos;
            return make_int(0);
        }

        p->pos++;
        return value;
    }

    p->error = 1;
    p->error_pos = p->base_pos + p->pos;
    return make_int(0);
}

/*
   Recursive call on the right makes ** right-associative.
   Example: 2 ** 3 ** 2 = 2 ** (3 ** 2).
*/
static Value parse_power(Parser *p)
{
    Value base = parse_primary(p);

    if (p->error)
    {
        return make_int(0);
    }

    skip_spaces(p);

    if (p->text[p->pos] == '*' && p->text[p->pos + 1] == '*')
    {
        size_t operator_pos = p->pos;

        p->pos += 2;
        skip_spaces(p);

        size_t exponent_start = p->pos;

        Value exponent = parse_power(p);

        if (p->error)
        {
            return make_int(0);
        }

        if (!base.is_float && !exponent.is_float)
        {
            if (exponent.i < 0)
            {
                p->error = 1;
                p->error_pos = p->base_pos + exponent_start;
                return make_int(0);
            }

            long long result = 1;
            long long b = base.i;
            long long e = exponent.i;

            while (e > 0)
            {
                if (e % 2 == 1)
                {
                    result *= b;
                }

                e /= 2;

                if (e > 0)
                {
                    b *= b;
                }
            }

            return make_int(result);
        }

        errno = 0;

        double result = pow(
            value_as_double(base),
            value_as_double(exponent)
        );

        if (errno != 0 || !isfinite(result))
        {
            p->error = 1;
            p->error_pos = p->base_pos + operator_pos;
            return make_int(0);
        }

        return make_float(result);
    }

    return base;
}

static Value parse_term(Parser *p)
{
    Value result = parse_power(p);

    if (p->error)
    {
        return make_int(0);
    }

    while (1)
    {
        skip_spaces(p);

        if (p->text[p->pos] == '*' && p->text[p->pos + 1] == '*')
        {
            break;
        }

        char op = p->text[p->pos];

        if (op != '*' && op != '/')
        {
            break;
        }

        size_t operator_pos = p->pos;

        p->pos++;
        skip_spaces(p);

        size_t operand_start = p->pos;

        Value right = parse_power(p);

        if (p->error)
        {
            return make_int(0);
        }

        if (op == '/')
        {
            int zero = right.is_float ? (right.d == 0.0) : (right.i == 0);

            if (zero)
            {
                p->error = 1;
                p->error_pos = p->base_pos + operand_start;
                return make_int(0);
            }
        }

        if (!result.is_float && !right.is_float)
        {
            if (op == '*')
            {
                result.i *= right.i;
            }
            else
            {
                result.i /= right.i;
            }

            result.d = (double)result.i;
        }
        else
        {
            double left_value = value_as_double(result);
            double right_value = value_as_double(right);
            double answer;

            if (op == '*')
            {
                answer = left_value * right_value;
            }
            else
            {
                answer = left_value / right_value;
            }

            if (!isfinite(answer))
            {
                p->error = 1;
                p->error_pos = p->base_pos + operator_pos;
                return make_int(0);
            }

            result = make_float(answer);
        }
    }

    return result;
}

static Value parse_expression(Parser *p)
{
    Value result = parse_term(p);

    if (p->error)
    {
        return make_int(0);
    }

    while (1)
    {
        skip_spaces(p);

        char op = p->text[p->pos];

        if (op == '\0' || op == '\n' || op == ')')
        {
            break;
        }

        if (op != '+' && op != '-')
        {
            p->error = 1;
            p->error_pos = p->base_pos + p->pos;
            return make_int(0);
        }

        size_t operator_pos = p->pos;

        p->pos++;

        Value right = parse_term(p);

        if (p->error)
        {
            return make_int(0);
        }

        if (!result.is_float && !right.is_float)
        {
            if (op == '+')
            {
                result.i += right.i;
            }
            else
            {
                result.i -= right.i;
            }

            result.d = (double)result.i;
        }
        else
        {
            double left_value = value_as_double(result);
            double right_value = value_as_double(right);
            double answer;

            if (op == '+')
            {
                answer = left_value + right_value;
            }
            else
            {
                answer = left_value - right_value;
            }

            if (!isfinite(answer))
            {
                p->error = 1;
                p->error_pos = p->base_pos + operator_pos;
                return make_int(0);
            }

            result = make_float(answer);
        }
    }

    return result;
}

static int line_is_blank_or_comment(const char *line)
{
    size_t i = 0;

    while (line[i] == ' ' || line[i] == '\t')
    {
        i++;
    }

    if (line[i] == '\n' || line[i] == '\0')
    {
        return 1;
    }

    if (line[i] == '#')
    {
        return 1;
    }

    return 0;
}

/*
   Dynamic line reading avoids a fixed line length limit.
*/
static char *read_line(FILE *file, size_t *length)
{
    size_t capacity = 256;
    size_t len = 0;

    char *buffer = malloc(capacity);

    if (buffer == NULL)
    {
        return NULL;
    }

    int c;

    while ((c = fgetc(file)) != EOF)
    {
        if (len + 1 >= capacity)
        {
            size_t new_capacity = capacity * 2;

            char *new_buffer = realloc(buffer, new_capacity);

            if (new_buffer == NULL)
            {
                free(buffer);
                return NULL;
            }

            buffer = new_buffer;
            capacity = new_capacity;
        }

        buffer[len++] = (char)c;

        if (c == '\n')
        {
            break;
        }
    }

    if (len == 0 && c == EOF)
    {
        free(buffer);
        return NULL;
    }

    buffer[len] = '\0';
    *length = len;

    return buffer;
}

static void get_base_name(
    const char *path,
    char *result,
    size_t result_size,
    int remove_txt
)
{
    size_t len = strlen(path);

    while (len > 1 && path[len - 1] == '/')
    {
        len--;
    }

    size_t start = len;

    while (start > 0 && path[start - 1] != '/')
    {
        start--;
    }

    size_t name_len = len - start;

    if (name_len >= result_size)
    {
        name_len = result_size - 1;
    }

    memcpy(result, path + start, name_len);
    result[name_len] = '\0';

    if (remove_txt)
    {
        char *dot = strrchr(result, '.');

        if (dot != NULL && strcmp(dot, ".txt") == 0)
        {
            *dot = '\0';
        }
    }
}

static int make_output_directory(const char *outdir)
{
    if (mkdir(outdir, 0777) != 0)
    {
        if (errno != EEXIST)
        {
            fprintf(
                stderr,
                "Cannot create output directory: %s\n",
                outdir
            );

            return 1;
        }
    }

    return 0;
}

static int evaluate_file(
    const char *input_file,
    const char *outdir
)
{
    FILE *input = fopen(input_file, "r");

    if (input == NULL)
    {
        fprintf(
            stderr,
            "Cannot open input file: %s\n",
            input_file
        );

        return 1;
    }

    char base[512];

    get_base_name(
        input_file,
        base,
        sizeof(base),
        1
    );

    char output_path[1024];

    int written = snprintf(
        output_path,
        sizeof(output_path),
        "%s/%s_aykhan_asadov_251ADC004.txt",
        outdir,
        base
    );

    if (written < 0 || (size_t)written >= sizeof(output_path))
    {
        fprintf(stderr, "Output path is too long.\n");
        fclose(input);
        return 1;
    }

    FILE *output = fopen(output_path, "w");

    if (output == NULL)
    {
        fprintf(
            stderr,
            "Cannot open output file: %s\n",
            output_path
        );

        fclose(input);
        return 1;
    }

    size_t file_pos = 1;

    while (1)
    {
        size_t line_length;

        char *line = read_line(
            input,
            &line_length
        );

        if (line == NULL)
        {
            if (ferror(input))
            {
                fprintf(
                    stderr,
                    "Error while reading input file.\n"
                );

                fclose(output);
                fclose(input);

                return 1;
            }

            break;
        }

        if (line_is_blank_or_comment(line))
        {
            file_pos += line_length;
            free(line);
            continue;
        }

        Parser parser;

        parser.text = line;
        parser.pos = 0;
        parser.base_pos = file_pos;
        parser.error_pos = 0;
        parser.error = 0;

        Value result = parse_expression(&parser);

        if (!parser.error)
        {
            skip_spaces(&parser);

            char c = parser.text[parser.pos];

            if (c == ')')
            {
                parser.error = 1;
                parser.error_pos = parser.base_pos + parser.pos;
            }
            else if (c != '\0' && c != '\n')
            {
                parser.error = 1;
                parser.error_pos = parser.base_pos + parser.pos;
            }
        }

        if (parser.error)
        {
            size_t error_position = parser.error_pos;

            free(line);
            fclose(output);

            output = fopen(output_path, "w");

            if (output == NULL)
            {
                fprintf(
                    stderr,
                    "Cannot reopen output file.\n"
                );

                fclose(input);
                return 1;
            }

            fprintf(
                output,
                "ERROR:%zu\n",
                error_position
            );

            fclose(output);
            fclose(input);

            return 0;
        }

        if (result.is_float)
        {
            fprintf(
                output,
                "%.15g\n",
                result.d
            );
        }
        else
        {
            fprintf(
                output,
                "%lld\n",
                result.i
            );
        }

        file_pos += line_length;
        free(line);
    }

    fclose(output);
    fclose(input);

    return 0;
}

static int has_txt_extension(const char *name)
{
    size_t len = strlen(name);

    return (
        len >= 4 &&
        strcmp(name + len - 4, ".txt") == 0
    );
}

static int evaluate_directory(
    const char *directory,
    const char *outdir
)
{
    DIR *dir = opendir(directory);

    if (dir == NULL)
    {
        fprintf(
            stderr,
            "Cannot open directory: %s\n",
            directory
        );

        return 1;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        if (!has_txt_extension(entry->d_name))
        {
            continue;
        }

        char input_path[1024];

        int written = snprintf(
            input_path,
            sizeof(input_path),
            "%s/%s",
            directory,
            entry->d_name
        );

        if (written < 0 || (size_t)written >= sizeof(input_path))
        {
            fprintf(stderr, "Input path is too long.\n");
            closedir(dir);
            return 1;
        }

        struct stat info;

        if (stat(input_path, &info) != 0)
        {
            fprintf(
                stderr,
                "Cannot inspect file: %s\n",
                input_path
            );

            closedir(dir);
            return 1;
        }

        if (!S_ISREG(info.st_mode))
        {
            continue;
        }

        if (evaluate_file(input_path, outdir) != 0)
        {
            closedir(dir);
            return 1;
        }
    }

    closedir(dir);

    return 0;
}

int main(int argc, char *argv[])
{
    const char *input_file = NULL;
    const char *input_dir = NULL;
    const char *outdir = NULL;

    int i = 1;

    while (i < argc)
    {
        if (
            strcmp(argv[i], "-o") == 0 ||
            strcmp(argv[i], "--output-dir") == 0
        )
        {
            if (i + 1 >= argc)
            {
                fprintf(
                    stderr,
                    "Missing output directory.\n"
                );

                return 1;
            }

            outdir = argv[i + 1];
            i += 2;
        }
        else if (
            strcmp(argv[i], "-d") == 0 ||
            strcmp(argv[i], "--dir") == 0
        )
        {
            if (i + 1 >= argc)
            {
                fprintf(
                    stderr,
                    "Missing input directory.\n"
                );

                return 1;
            }

            if (input_dir != NULL)
            {
                fprintf(
                    stderr,
                    "Input directory specified more than once.\n"
                );

                return 1;
            }

            input_dir = argv[i + 1];
            i += 2;
        }
        else if (argv[i][0] == '-')
        {
            fprintf(
                stderr,
                "Unknown option: %s\n",
                argv[i]
            );

            return 1;
        }
        else
        {
            if (input_file != NULL)
            {
                fprintf(
                    stderr,
                    "More than one input file specified.\n"
                );

                return 1;
            }

            input_file = argv[i];
            i++;
        }
    }

    if (input_dir != NULL && input_file != NULL)
    {
        fprintf(
            stderr,
            "Use either an input file or -d, not both.\n"
        );

        return 1;
    }

    if (input_dir == NULL && input_file == NULL)
    {
        fprintf(
            stderr,
            "No input file or directory specified.\n"
        );

        return 1;
    }

    char default_outdir[1024];

    if (outdir == NULL)
    {
        char base[512];

        if (input_file != NULL)
        {
            get_base_name(
                input_file,
                base,
                sizeof(base),
                1
            );
        }
        else
        {
            get_base_name(
                input_dir,
                base,
                sizeof(base),
                0
            );
        }

        int written = snprintf(
            default_outdir,
            sizeof(default_outdir),
            "%s_aykhan_251ADC004",
            base
        );

        if (written < 0 || (size_t)written >= sizeof(default_outdir))
        {
            fprintf(
                stderr,
                "Output directory name is too long.\n"
            );

            return 1;
        }

        outdir = default_outdir;
    }

    if (make_output_directory(outdir) != 0)
    {
        return 1;
    }

    if (input_file != NULL)
    {
        return evaluate_file(
            input_file,
            outdir
        );
    }

    return evaluate_directory(
        input_dir,
        outdir
    );
}