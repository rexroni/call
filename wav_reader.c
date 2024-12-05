#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char *ptr;
    size_t len;
} buf_t;

typedef char fourcc_t[5];

typedef struct {
    fourcc_t type;
    buf_t body;
    buf_t leftover;
} chunk_t;

void read_fourcc(const char *ptr, fourcc_t *out){
    (*out)[0] = ptr[0];
    (*out)[1] = ptr[1];
    (*out)[2] = ptr[2];
    (*out)[3] = ptr[3];
    (*out)[4] = '\0';
}

// read little-endian u16 from any host
uint16_t read_u16(const char *ptr){
    const unsigned char *u = (const unsigned char*)ptr;
    return u[0] | (u[1] << 8);
}

// read little-endian u32 from any host
uint32_t read_u32(const char *ptr){
    const unsigned char *u = (const unsigned char*)ptr;
    return u[0] | (u[1] << 8) | (u[2] << 16) | (u[3] << 24);
}

#define FAIL(msg) { fprintf(stderr, msg "\n"); return -1; }

int read_chunk(buf_t in, chunk_t *out){
    if(in.len < 8) FAIL("incomplete chunk");
    read_fourcc(in.ptr, &out->type);
    uint32_t len = read_u32(in.ptr + 4);
    // if chunk len is odd, expect a padding byte too
    size_t full_len = len + 8 + (len % 2);
    if(full_len > in.len) FAIL("incorrect chunk length");
    out->body.len = len;
    out->body.ptr = in.ptr + 8;
    out->leftover.ptr = in.ptr + full_len;
    out->leftover.len = in.len - full_len;
    return 0;
}

// read a sample, mapping it to the uint16_t range
uint16_t read_sample8(const unsigned char *u){
    // sample is 0 to 255
    return (uint16_t)(*u) << 8;
}

uint16_t read_sample16(const unsigned char *u){
    // sample is -2**15 to 2**15-1
    return (
        ((uint16_t)(u[1]) << 8) | (uint16_t)(u[0])
    ) + (uint16_t)(1 << 15);
}

uint16_t read_sample24(const unsigned char *u){
    // sample is -2**23 to 2**23-1
    return (
        ((uint16_t)(u[2]) << 8) | (uint16_t)(u[1])
    ) + (uint16_t)(1 << 15);
}

uint16_t read_sample32(const unsigned char *u){
    // sample is -2**31 to 2**31-1
    return (
        ((uint16_t)(u[3]) << 8) | (uint16_t)(u[2])
    ) + (uint16_t)(1 << 15);
}

int read_wav(const char *buf, size_t len, FILE *f){
    buf_t input = { buf, len };
    chunk_t riff;
    int ret = read_chunk(input, &riff);
    if(ret) return ret;
    // should be one RIFF covering the whole file
    if(strcmp(riff.type, "RIFF")) FAIL("first chunk must be RIFF");
    if(riff.leftover.len > 0) FAIL("wrong RIFF chunk length");
    // RIFF is a type, then a list of chunks
    if(riff.body.len < 4) FAIL("RIFF chunk too short");
    fourcc_t wave;
    read_fourcc(riff.body.ptr, &wave);
    if(strcmp(wave, "WAVE")) FAIL("RIFF must be of subtype WAVE");
    buf_t chunks = { riff.body.ptr + 4, riff.body.len - 4 };
    // locate the fmt and data chunks
    chunk_t fmt = {0};
    chunk_t data = {0};
    while(chunks.len){
        chunk_t chunk;
        ret = read_chunk(chunks, &chunk);
        if(ret) return ret;
        if(strcmp(chunk.type, "fmt ") == 0){
            if(fmt.body.len) FAIL("duplicate fmt chunks");
            fmt = chunk;
        }else if(strcmp(chunk.type, "data") == 0){
            if(data.body.len) FAIL("duplicate data chunks");
            data = chunk;
        }
        chunks = chunk.leftover;
    }

    // we only support PCM encoding
    if(fmt.body.len < 2) FAIL("fmt section too short");
    uint16_t tag = read_u16(fmt.body.ptr);
    if(tag != 0x01) FAIL("only PCM encoding is supported");

    // read the PCM-specific fmt chunk
    if(fmt.body.len < 16) FAIL("fmt section too short");
    uint16_t channels = read_u16(fmt.body.ptr + 2);
    if(channels < 1) FAIL("must have at least 1 channel");
    uint32_t hz = read_u32(fmt.body.ptr + 4);
    uint16_t bits = read_u16(fmt.body.ptr + 14);
    if(bits < 8 || bits > 32) FAIL("bits must be between 8 and 32");

    if(!data.body.len) FAIL("data section missing");

    // prepare to read samples
    const unsigned char *udata = (const unsigned char *)data.body.ptr;
    size_t bytes_per_samp = (bits + 7) / 8;
    size_t nsamples = data.body.len / bytes_per_samp / channels;
    uint16_t (*read_sample)(const unsigned char *u);
    switch(bytes_per_samp){
        case 1: read_sample = read_sample8; break;
        case 2: read_sample = read_sample16; break;
        case 3: read_sample = read_sample24; break;
        case 4: read_sample = read_sample32; break;
    }

    // generate a C file with the wave data embedded into it
    fprintf(f, "const unsigned wav_channels = 1;\n");
    fprintf(f, "const unsigned wav_bits = 16;\n");
    fprintf(f, "const unsigned wav_bytes_per_sample = %zu;\n", bytes_per_samp);
    fprintf(f, "const unsigned wav_hz = %zu;\n", hz);
    fprintf(f, "const unsigned wav_samples = %zu;\n", nsamples);
    fprintf(f, "const unsigned char wav_data[] = {\n    \"");
    for(
        size_t i = 0, x = 0;
        i + bytes_per_samp * channels <= data.body.len;
        i += bytes_per_samp * channels, x++
    ){
        // 8 samples per line
        if(x % 8 == 0 && x) fprintf(f, "\"\n    \"");
        // merge samples from every channel into one
        uint32_t sample = 0;
        for(size_t j = 0; j < channels; j++){
            sample += read_sample(udata + i + j * bytes_per_samp);
        }
        sample /= channels;
        // rotate to center around 0, for 16-bit output
        uint16_t sample16 = ((uint16_t)sample) - (uint16_t)(1 << 15);
        // emit little-endian encoded values
        uint32_t high = (sample16 >> 8);
        uint32_t low = (sample16 & 0xff);
        fprintf(f, "\\x%.2x\\x%.2x", low, high);
    }
    fprintf(f, "\"\n};\n");

    return 0;
}

int main(int argc, char **argv){
    if(argc < 3){
        fprintf(stderr, "usage: %s INFILE.WAV OUT.C\n");
        return 1;
    }

    char *inpath = argv[1];
    char *outpath = argv[2];

    char *buf = NULL;
    int fd = -1;
    FILE *out = NULL;

    int retval = 1;

    // read the entire wav file
    size_t cap = 65536;
    size_t len = 0;
    buf = malloc(cap);
    if(!buf){
        perror("malloc");
        goto cu;
    }
    fd = open(inpath, O_RDONLY);
    if(fd < 0){
        perror(inpath);
        goto cu;
    }
read_more:
    ssize_t zret = read(fd, buf + len, cap - len);
    if(zret < 0){
        perror("read");
        goto cu;
    }
    len += (size_t)zret;
    if(len == cap){
        // the read filled the buffer; grow it and try again
        cap *= 2;
        char *temp = realloc(buf, cap);
        if(!temp){
            perror("realloc");
            goto cu;
        }
        buf = temp;
        goto read_more;
    }
    close(fd); fd = -1;

    out = fopen(outpath, "w");
    if(!out){
        perror(outpath);
        goto cu;
    }

    retval = read_wav(buf, len, out);
    if(retval) goto cu;

    // write FILE* buffers to disk
    int ret = fflush(out);
    if(ret){
        perror(outpath);
        goto cu;
    }

    // wait for writes to physically complete
    ret = fsync(fileno(out));
    if(ret){
        perror(outpath);
        goto cu;
    }

cu:
    if(buf) free(buf);
    if(fd > -1) close(fd);
    if(out) fclose(out);
    return retval;
}
