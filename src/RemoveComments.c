#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    FILE *srcCode;
    FILE *onlyCode;
    FILE *onlyComment;
} FileGroup;

static size_t freadc (char *ch, FILE *stream);
static size_t fwritec(const char *ch, FILE *stream);
static char   fpeekc (FILE *stream, int offset);

static int isBegin_BlockComment (char ch, FILE *srcCode);
static int Process_BlockComment (char ch, FileGroup *fileGroup);
static int isBegin_LineComment  (char ch, FILE *srcCode);
static int Process_LineComment  (char ch, FileGroup *fileGroup);
static int isBegin_StringLiteral(char ch, FILE *srcCode);
static int Process_StringLiteral(char ch, FileGroup *fileGroup);
static int Process_Default      (char ch, FileGroup *fileGroup);
static int EraseComment         (FileGroup *fileGroup);

static int   GetFilePath(char *result);
static char *AddSuffix  (const char *filePath, const char *suffix, char *resultPath);
static int   REPL       (void);
static int   CLI        (int argc, char *argv[]);

static size_t freadc(char *ch, FILE *stream)
{
    return fread(ch, sizeof(char), 1, stream);
}

static size_t fwritec(const char *ch, FILE *stream)
{
    return fwrite(ch, sizeof(char), 1, stream);
}

static char fpeekc(FILE *stream, int offset)
{
    char ch  = '\0';
    long cur = ftell(stream);

    if (0 != fseek(stream, offset, SEEK_CUR))
    {
        /* WHY 2023/07/24
           fseek() 如果执行失败，理应不移动文件指针，
           但事实是文件指针被移动了，需要复位
        */
        goto return_; 
    }

    freadc(&ch, stream);

return_:
    fseek(stream, cur, SEEK_SET);
    return ch;
}

static int isBegin_BlockComment(char ch, FILE *srcCode)
{
    return ch == '/' && fpeekc(srcCode, 0) == '*';
}

static int Process_BlockComment(char ch, FileGroup *fileGroup)
{
    FILE *srcCode     = fileGroup->srcCode;
    FILE *onlyComment = fileGroup->onlyComment;

    char c = '\0';

    /* 由 isBegin_BlockComment() 保证： */
    c = ch;                   /* '/' */
    fwritec(&c, onlyComment);
    freadc(&c, srcCode);      /* '*' */
    fwritec(&c, onlyComment);

    while (0 != freadc(&c, srcCode))
    {
        fwritec(&c, onlyComment);

        if (c == '/' && fpeekc(srcCode, -2) == '*') {
            break;
        }
    }

    return 0;
}

static int isBegin_LineComment(char ch, FILE *srcCode)
{
    return ch == '/' && fpeekc(srcCode, 0) == '/';
}

static int Process_LineComment(char ch, FileGroup *fileGroup)
{
    FILE *srcCode     = fileGroup->srcCode;
    FILE *onlyCode    = fileGroup->onlyCode;
    FILE *onlyComment = fileGroup->onlyComment;

    char c    = '\0';
    char prev = '\0';

    /* 由 isBegin_LineComment() 保证： */
    c = ch;                   /* '/' */
    fwritec(&c, onlyComment);
    freadc(&c, srcCode);      /* '/' */
    fwritec(&c, onlyComment);

    while (0 != freadc(&c, srcCode))
    {
        fwritec(&c, onlyComment);

        if (c == '\n')
        {
            prev = fpeekc(srcCode, -2);
            if ( ! (    (prev == '\r' && fpeekc(srcCode, -3) == '\\')
                      || prev == '\\'                                 ))
            {
                break;
            }
        }
    }

    if (prev == '\r') {
        fwritec((char[]) {'\r'}, onlyCode);
    }
    fwritec((char[]) {'\n'}, onlyCode);

    return 0;
}

static int isBegin_StringLiteral(char ch, FILE *srcCode)
{
    return ch == '\"' && fpeekc(srcCode, -2) != '\\';
}

static int Process_StringLiteral(char ch, FileGroup *fileGroup)
{
    FILE *srcCode     = fileGroup->srcCode;
    FILE *onlyCode    = fileGroup->onlyCode;
    FILE *onlyComment = fileGroup->onlyComment;

    char c = '\0';
    
    /* 由 isBegin_StringLiteral() 保证： */
    c = ch; /* '\"' */
    fwritec(&c, onlyCode);
    fwritec((char[]) {' '}, onlyComment);

    while (0 != freadc(&c, srcCode))
    {
        fwritec(&c, onlyCode);
        fwritec((char[]) {' '}, onlyComment);

        if (c == '\"' && fpeekc(srcCode, -2) != '\\') {
            break;
        }
    }

    return 0;
}

static int Process_Default(char ch, FileGroup *fileGroup)
{
    FILE *onlyCode    = fileGroup->onlyCode;
    FILE *onlyComment = fileGroup->onlyComment;

    fwritec(&ch, onlyCode);
    fwritec(strchr("\n\r\t ", ch) ? &ch : (char[]) {' '}, onlyComment);

    return 0;
}

static int EraseComment(FileGroup *fileGroup)
{
    FILE *srcCode = fileGroup->srcCode;

    char ch = '\0';
    while (0 != freadc(&ch, srcCode))
    {
        if (isBegin_BlockComment(ch, srcCode)) {
            Process_BlockComment(ch, fileGroup);
        }
        else if (isBegin_LineComment(ch, srcCode)) {
            Process_LineComment(ch, fileGroup);
        }
        else if (isBegin_StringLiteral(ch, srcCode)) {
            Process_StringLiteral(ch, fileGroup);
        }
        else {
            Process_Default(ch, fileGroup);
        }
    }

    return 0;
}

static int GetFilePath(char *result)
{
    rewind(stdin);
    if (1 != scanf("%250[^\n]s", result)) {
        return -1;
    }
    
    size_t len = strlen(result);
    if ('\"' != result[0] && '\"' != result[len - 1]) {
        return 0;
    }

    if (len <= 2 || '\"' != result[0] || '\"' != result[len - 1]) {
        return -1;
    }

    memmove(result, &result[1], len - 2);
    result[len - 2] = '\0';

    return 0;
}

static char *AddSuffix(const char *filePath, const char *suffix, char *result)
{
    size_t filePathLength = strlen(filePath);
    size_t suffixLength   = strlen(suffix);

    strcpy(result, filePath);
    char *pointIndex      = strrchr(result, '.');

    if (NULL == pointIndex) {
        pointIndex = &result[filePathLength];
    }

    memmove(pointIndex + suffixLength, pointIndex, strlen(pointIndex));
    memcpy(pointIndex, suffix, suffixLength); /* 不能使用strcpy()，strcpy()插入'\0' */

    return result;
}

static int REPL(void)
{
    puts("\n ==[ Remove comments from C code ]==\n");

    char  srcCode[_MAX_PATH]     = { 0 };
    char  onlyCode[_MAX_PATH]    = { 0 };
    char  onlyComment[_MAX_PATH] = { 0 };

    puts(" -> source file path:");
    if (0 != GetFilePath(srcCode)) {
        goto return_;
    }
    
    puts(" -> save the result code in: (if left blank, use the default path)");
    GetFilePath(onlyCode);

    puts(" -> save the comment in:     (if left blank, use the default path)");
    GetFilePath(onlyComment);

return_:
    return CLI(4, (char *[]) {"", srcCode, onlyCode, onlyComment });
}

static int CLI(int argc, char *argv[])
{
    if (argc <= 1)
    {
        fprintf(stderr, "error: too few args, unknow source code path\n");
        goto return_;
    }
    else if (0 == strcmp(argv[1], ""))
    {
        fprintf(stderr, "error: unknow source code path\n");
        goto return_;
    }

    char *srcCode     = argv[1];
    char *onlyCode    = (argc <= 2 || 0 == strcmp(argv[2], "")) ? AddSuffix(argv[1], "_C", (char[_MAX_PATH]) { 0 })
                                                                : argv[2];
    char *onlyComment = (argc <= 3 || 0 == strcmp(argv[3], "")) ? AddSuffix(argv[1], "_A", (char[_MAX_PATH]) { 0 })
                                                                : argv[3];

    printf("\n"
           "source code from    \"%s\"\n"
           "save result code to \"%s\"\n"
           "save comment to     \"%s\"\n"
           "\n",
           srcCode, onlyCode, onlyComment
    );

    if (0 == strcmp(srcCode, onlyCode)) {
        fprintf(stderr, "error: source code path must be different from the save result code path\n");
        goto return_;
    }
    if (0 == strcmp(srcCode, onlyComment)) {
        fprintf(stderr, "error: source code path must be different from the save comment path\n");
        goto return_;
    }
    if (0 == strcmp(onlyCode, onlyComment)) {
        fprintf(stderr, "error: save result code path must be different from the save comment path\n");
        goto return_;
    }

    FILE *files[3] = { 0 };

    files[0] = fopen(srcCode, "rb");
    if (NULL == files[0])
    {
        fprintf(stderr, "error: can not open file: \"%s\"\n", srcCode);
        goto return_;
    }

    files[1] = fopen(onlyCode, "wb");
    if (NULL == files[1])
    {
        fprintf(stderr, "error: can not open file: \"%s\"\n", onlyCode);
        goto closeFiles_;
    }

    files[2] = fopen(onlyComment, "wb");
    if (NULL == files[2])
    {
        fprintf(stderr, "error: can not open file: \"%s\"\n", onlyComment);
        goto closeFiles_;
    }

    puts("...");

    EraseComment(&((FileGroup) {
        .srcCode     = files[0],
        .onlyCode    = files[1],
        .onlyComment = files[2],
    }));

    puts("finished!");

closeFiles_:
    for (int i = 0; i < 3; i++)
    {
        if (NULL != files[i]) {
            fclose(files[i]);
        }
    }

return_:
    puts("\npress any key to exit ...");
    rewind(stdin);
    (void)getchar();

    return 0;
}

int main(int argc, char *argv[])
{
    return (argc == 1 || argc > 4) ? REPL()
                                   : CLI(argc, argv);
}
