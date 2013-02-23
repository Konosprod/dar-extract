#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include <assert.h>
#include <dirent.h>
#include <string.h>
#if defined WIN32
    #include <sys/types.h>
#endif

#define CHUNK 16384

typedef struct Token
{
    int offset_string;
    int csize;
    int usize;
    int offset;
}Token;

typedef struct Header
{
    int nbfile;
    int beg_ftab;
    int size_token;
    int beg_dtab;
    Token* tok;
}Header;

int nombre_dossier(char chaine[])
{
    int i = 0;
    int nb = 0;

    while(chaine[i] != '\0')
    {
        if(chaine[i] == '/')
        {
            nb++;
        }
        i++;
    }
    return nb;
}

void make_dirs(char* out)
{
    char temp[250] = {0};
    int i = 0;
    int nb = 0;
    char* dir = NULL;
    char path[250] = {0};
    DIR* directory = NULL;

    sprintf(temp, "%s", out);

    dir = strtok(temp, "/");

    if(dir != NULL)
    {

        nb = nombre_dossier(out);

        for(i = 0; i < nb; i++)
        {
            strcat(path, dir);
            strcat(path, "/");
            directory = opendir(path);
            if(!directory)
            {
                mkdir(path);
                closedir(directory);
            }
            else
            {
                closedir(directory);
            }

            dir = strtok(NULL, "/");
        }
    }
}

int inflate2(char* str_src, char* str_dest)
{
    FILE *source;
    FILE *dest;
    int ret;
    unsigned have;
    z_stream strm;
    char in[CHUNK];
    char out[CHUNK];

    source  = fopen(str_src, "rb");
    dest    = fopen(str_dest, "wb");

    /* allocate inflate state */

    strm.zalloc   = Z_NULL;
    strm.zfree    = Z_NULL;
    strm.opaque   = Z_NULL;
    strm.next_in  = Z_NULL;
    strm.avail_in = 0;

    ret = inflateInit(&strm);

    if (ret != Z_OK) return ret;

    /* decompress until deflate stream ends or end of file */
    do
    {
        strm.avail_in = fread(in, 1, CHUNK, source);

        if (ferror(source))
        {
            inflateEnd(&strm);
            fclose(source);
            fclose(dest);
            return Z_ERRNO;
        }
        if (strm.avail_in == 0) break;

        strm.next_in = (Bytef*) in;

        /* run inflate() on input until output buffer not full */
        do
        {
            strm.avail_out = CHUNK;
            strm.next_out  = (Bytef*) out;

            ret = inflate(&strm, Z_NO_FLUSH);

            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret)
            {
                case Z_NEED_DICT:
                {
                    ret = Z_DATA_ERROR;     /* and fall through */
                }
                case Z_DATA_ERROR:
                case Z_MEM_ERROR:
                {
                    inflateEnd(&strm);
                    fclose(source);
                    fclose(dest);
                    return ret;
                }
            }
            have = CHUNK - strm.avail_out;

            if (fwrite(out, 1, have, dest) != have || ferror(dest))
            {
                inflateEnd(&strm);
                fclose(source);
                fclose(dest);
                return Z_ERRNO;
            }
        }
        while (strm.avail_out == 0);

        assert(strm.avail_in == 0);     /* all input will be used */

        /* done when inflate() says it's done */
    }
    while (ret != Z_STREAM_END);

    /* clean up and return */

    inflateEnd(&strm);

    fclose(source);
    fclose(dest);

    return (ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR);
}

void get_header(Header* h, FILE* in)
{
    fread(&h->nbfile, sizeof(char), 4, in);
    fread(&h->beg_ftab, sizeof(char), 4, in);
    fread(&h->size_token, sizeof(char), 4, in);
    fread(&h->beg_dtab, sizeof(char), 4, in);
    printf("Files : %.4X\n", h->nbfile);
    printf("Offset file table : 0x%.8X\n", h->beg_ftab);
    printf("Token size : 0x%.8X\n", h->size_token);
    printf("Offset data table : 0x%.8X\n", h->beg_dtab);

    h->tok = calloc(sizeof(Token), h->nbfile);

    if(h->tok == NULL)
    {
        printf("Bad alloc\n");
        exit(EXIT_FAILURE);
    }

    fseek(in, h->beg_dtab, SEEK_SET);

    for(int i = 0; i < h->nbfile; i++)
    {
        fread(&h->tok[i].offset_string, sizeof(char), 4, in);
        fread(&h->tok[i].csize, sizeof(char), 4, in);
        fread(&h->tok[i].usize, sizeof(char), 4, in);
        fread(&h->tok[i].offset, sizeof(char), 4, in);
        //printf("File[%X]: CS: 0x%.8X US: 0x%.8X Offset : 0x%.8X OFS: 0x%.8X\n", i, h->tok[i].csize, h->tok[i].usize, h->tok[i].offset, h->tok[i].offset_string);
    }
}

void dump_files(Header* h, FILE* in)
{
    unsigned char* udata = NULL;
    unsigned char* cdata = NULL;
    FILE* out = NULL;
    char fname[100] = {0};
    char a = 'a';
    int b = 0;
    int ret = 0;

    for(int i = 0; i < h->nbfile; i++)
    {
        printf("Dumping file %d...", i+1);
        fflush(stdout);
        fseek(in, h->tok[i].offset_string, SEEK_SET);

        while(a != '\0')
        {
            fread(&a, sizeof(char), 1, in);
            fname[b] = a;
            b++;
        }
        b = 0;
        a = 'a';

        make_dirs(fname);
        out = fopen(fname, "wb+");
        fseek(in, h->tok[i].offset, SEEK_SET);
        udata = calloc(h->tok[i].usize, sizeof(char));
        cdata = calloc(h->tok[i].csize, sizeof(char));
        fread(cdata, sizeof(char), h->tok[i].csize, in);
        ret = uncompress(udata, &h->tok[i].usize, cdata, h->tok[i].csize);
        switch(ret)
        {
            case Z_OK:
            break;

            case Z_MEM_ERROR:
                printf("Erreur memoire\n");
            break;

            case Z_BUF_ERROR:
                printf("Erreur buffer\n");
            break;

            case Z_DATA_ERROR:
                printf("Erreur data\n");
            break;
        }
        fwrite(udata, sizeof(char), h->tok[i].usize, out);
        fclose(out);
        sprintf(fname, "");
        free(udata);
        free(cdata);
        printf("...done\n");
    }

}

int main(int argc, char* argv[])
{
    Header h = {0, 0, 0, 0, NULL};
    FILE* in = fopen(argv[1], "rb");

    if(!in)
    {
        printf("Impossible d'ouvrir data.dar\n");
        return EXIT_FAILURE;
    }

    get_header(&h, in);
    dump_files(&h, in);
    remove("tmp");
    free(h.tok);

    return 0;
}
