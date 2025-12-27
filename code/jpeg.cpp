#include "common.h"
#include "jpeg.h"

JPEG_BUFFER init_buffer(){
    JPEG_BUFFER b = {0};
    b.length = 0;
    b.size = 64;
    b.data = (uint8_t*) malloc(b.size);
    b.acc = 0;
    b.bits = 0;
    return b;
}

// (AI) (Let's just hope it does not fuck up)
int build_huffman(
    const uint8_t *nr,
    int max_symbols,
    uint16_t *codes,
    uint8_t *sizes
) {
    uint16_t code = 0;
    int k = 0;
    for (int bits = 1; bits <= 16; bits++) {
        code <<= 1;
        for (int i = 0; i < nr[bits]; i++) {
            if (k >= max_symbols) return k;
            codes[k] = code;
            sizes[k] = bits;
            code++;
            k++;
        }
    }
    return k;
}

// (AI) (Let's just hope it does not fuck up)
void generate_dc_table(
    const uint8_t *nr,
    const uint8_t *sym,
    DCHuff out[12]
) {
    uint16_t codes[12];
    uint8_t  sizes[12];

    build_huffman(nr, 12, codes, sizes);

    for (int i = 0; i < 12; i++) {
        out[sym[i]].code = codes[i];
        out[sym[i]].bits = sizes[i];
    }
}

// (AI)
int generate_ac_table(
    const uint8_t *nr,
    const uint8_t *sym,
    ACHuff *out
) {
    uint16_t codes[256];
    uint8_t  sizes[256];

    int count = build_huffman(nr, 256, codes, sizes);
    for (int i = 0; i < count; i++) {
        out[i].rs  = sym[i];
        out[i].code = codes[i];
        out[i].bits = sizes[i];
    }
    return count;
}

void write_app0(FILE *f){
    write_marker(f, 0xE0);
    write_word(f, 16);
    fwrite("JFIF\0", 1, 5, f);
    fputc(1,f); fputc(1,f);
    fputc(0,f);
    write_word(f, 1); write_word(f, 1);
    fputc(0,f); fputc(0,f);
}
void write_dqt(FILE *f, const uint8_t q[8][8], uint8_t id){
    write_marker(f, 0xDB);
    write_word(f, 2 + 1 + 64);
    fputc(id & 0x0F, f);
    for(int i=0;i<64;i++){
        int u = zigzag[i][0];
        int v = zigzag[i][1];
        fputc(q[u][v], f);
    };
}
void write_dht(
    FILE *f,
    uint8_t table_class,      // 0 = DC, 1 = AC
    uint8_t table_id,         // 0 = luma, 1 = chroma
    const uint8_t *nr,
    const uint8_t *sym,
    int symcount
){

    int sum = 0;
    for (int i = 1; i <= 16; i++) sum += nr[i];

    write_marker(f, 0xC4);
    write_word(f, 2 + 1 + 16 + symcount);

    fputc((table_class << 4) | (table_id & 0x0F), f);

    for (int i = 1; i <= 16; i++)
        fputc(nr[i], f);

    fwrite(sym, 1, symcount, f);
}
void write_sof0(FILE *f, int width, int height){
    write_marker(f, 0xC0);
    write_word(f, 8 + 3*3);
    // write_word(f, 8 + 3*1);
    fputc(8,f); // precision
    write_word(f, height);
    write_word(f, width);
    fputc(3,f); // components
    // fputc(1,f); // components
    // Y
    fputc(1,f); fputc(0x11,f); fputc(0,f); 
    // Cb
    fputc(2,f); fputc(0x11,f); fputc(1,f);
    // Cr
    fputc(3,f); fputc(0x11,f); fputc(1,f);
}
void write_sos_and_scan(FILE *f, JPEG_BUFFER *b){
    write_marker(f, 0xDA);
    write_word(f, 6 + 2*3);
    // write_word(f, 6 + 2*1);
    fputc(3,f);
    // fputc(1,f);
    // Y
    fputc(1,f); fputc((0<<4)|0,f);
    // Cb
    fputc(2,f); fputc((1<<4)|1,f);
    // Cr
    fputc(3,f); fputc((1<<4)|1,f);
    fputc(0,f); fputc(63,f); fputc(0,f); // Ss, Se, AhAl
    // Now write stuffed compressed data
    for (int i = 0; i < b->length; i++){
        uint8_t v = b->data[i];
        fputc(v, f);
        if (v == 0xFF) {
            fputc(0x00, f);
        }
    }
}
int write_jpeg_file(const char *path, JPEG_BUFFER *b, int width, int height){
    FILE *f = fopen(path, "wb");

    if(!f) return -1;
    write_marker(f, 0xD8);
    write_app0(f);
    write_dqt(f, QY, 0);
    write_dqt(f, QC, 1);
    write_sof0(f, width, height);
    write_dht(f, 0, 0, STD_DC_LUMA_NR,   STD_DC_LUMA_SYM,   12);
    write_dht(f, 1, 0, STD_AC_LUMA_NR,   STD_AC_LUMA_SYM,   162);
    write_dht(f, 0, 1, STD_DC_CHROMA_NR, STD_DC_CHROMA_SYM, 12);
    write_dht(f, 1, 1, STD_AC_CHROMA_NR, STD_AC_CHROMA_SYM, 162);
    write_sos_and_scan(f, b);

    write_marker(f, 0xD9);
    fclose(f);
    return 0;
}

uint32_t encode_coeff(int value, int SIZE) {
    if (value >= 0) return value;
    return ((1 << SIZE) - 1) & (value - 1);
}
void acc_write(JPEG_BUFFER *dest, uint32_t bits, int count){
    dest->acc = (dest->acc << count) | (bits & ((1 << count) - 1));
    dest->bits += count;
    while(dest->bits >= 8){
        dest->bits-=8;
        while(dest->size <= dest->length + 1){
            dest->data = (uint8_t*) realloc(dest->data, dest->size * 2);
            dest->size *= 2;
        };
        uint32_t out = (dest->acc >> dest->bits) & 0xFF;
        dest->data[dest->length++] = out;
        dest->acc = dest->acc & ( ( 1 << dest->bits ) - 1 );
    }
}
void finalize_buffer(JPEG_BUFFER *b){
    if (b->bits > 0) {
        int pad = (8 - (b->bits % 8)) % 8;
        if (pad) {
            b->acc = (b->acc << pad) | ((1u << pad) - 1); // pad with 1s
            b->bits += pad;
        }
        while (b->bits >= 8){
            b->bits -= 8;
            while(b->size <= b->length + 1){
                b->data = (uint8_t*) realloc(b->data, b->size * 2);
                b->size *= 2;
            }
            uint8_t out = (b->acc >> b->bits) & 0xFF;
            b->data[b->length++] = out;
            b->acc = b->acc & ((1u << b->bits) - 1);
        }
    }
}

void init_dct_table() {
    if (dct_initialized) return;
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            DCT_COSINE[i][j] = cos((2*i + 1) * j * M_PI / 16.0);
        }
    }
    dct_initialized = 1;
}

// void FDCT_8X8(const int input[8][8], int output[8][8]) {
//     for (int u = 0; u < 8; u++) {
//         for (int v = 0; v < 8; v++) {
//             int sum = 0;
//             for (int x = 0; x < 8; x++) {
//                 for (int y = 0; y < 8; y++) {
//                     sum += input[x][y] * cos((2*x + 1) * u * PI / 16.0) * cos((2*y + 1) * v * PI / 16.0);
//                 }
//             }
//             double cu = (u == 0) ? 1.0 / sqrt(2.0) : 1.0;
//             double cv = (v == 0) ? 1.0 / sqrt(2.0) : 1.0;
//             output[u][v] = 0.25 * cu * cv * sum;
//         }
//     }
// }

// (AI)
void FDCT_8X8(const int input[8][8], int output[8][8]) {
    if (!dct_initialized) init_dct_table();
    float temp[8][8];
    for (int x = 0; x < 8; x++) {
        for (int v = 0; v < 8; v++) {
            float sum = 0;
            for (int y = 0; y < 8; y++) {
                sum += input[x][y] * DCT_COSINE[y][v];
            }
            float cv = (v == 0) ? 0.5f / sqrtf(2.0f) : 0.5f;
            temp[x][v] = cv * sum;
        }
    }
    for (int u = 0; u < 8; u++) {
        for (int v = 0; v < 8; v++) {
            float sum = 0;
            for (int x = 0; x < 8; x++) {
                sum += temp[x][v] * DCT_COSINE[x][u];
            }
            float cu = (u == 0) ? 0.5f / sqrtf(2.0f) : 0.5f;
            output[u][v] = (int)(cu * sum);
        }
    }
}

void Quantize_8X8(const int input[8][8], int output[8][8], bool luminous) {
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if(luminous){
                output[y][x] = (int16_t)round(input[y][x]/QY[y][x]);
            }
            else{
                output[y][x] = (int16_t)round(input[y][x]/QC[y][x]);
            }
        }
    }
}

void ZigZag8x8(const int in[8][8], int16_t out[64]) {
    for (int i = 0; i < 64; i++) {
        int u = zigzag[i][0];
        int v = zigzag[i][1];
        out[i] = (int16_t)in[u][v];
    }
}

void EntropyEncode(JPEG_BUFFER *out, int lastDC, int16_t in[64], const DCHuff DC_TABLE[], const  ACHuff AC_TABLE[], ACHuff ZRL, ACHuff EOB){
    int DC_Diff = in[0] - lastDC;
    int SIZE = bit_length(DC_Diff);
    DCHuff DC_CODE = DC_TABLE[SIZE];
    acc_write(out, DC_CODE.code, DC_CODE.bits);
    if (SIZE > 0) {
        uint32_t mag = encode_coeff(DC_Diff, SIZE);
        acc_write(out, mag, SIZE);
    }
    int RUN = 0;
    for (int i = 1; i < 64; i++) {
        int AC = in[i];
        if(AC == 0){
            RUN++;
            if(RUN == 16){
                acc_write(out, ZRL.code, ZRL.bits);
                RUN = 0;
            }
        } 
        else{
            int mag = abs(AC);
            int SIZE = bit_length(AC);
            int RS = (RUN << 4) | SIZE;
            int found = 0;

            // TODO(Sergio): Make a lookup table directly indexed by rs [ N -> 1 ]
            for (int j = 0; j < AC_LUMA_COUNT; j++) {
                if (AC_TABLE[j].rs == RS) {
                    found = 1;
                    acc_write(out, AC_TABLE[j].code, AC_TABLE[j].bits);
                    break;
                }
            }

            if(!found){
                printf("error");
            }

            uint32_t magbits = encode_coeff(AC, SIZE);
            acc_write(out, magbits, SIZE);
            RUN = 0;
        }
    }
    if(RUN > 0){
        acc_write(out, EOB.code, EOB.bits);
    }
}
