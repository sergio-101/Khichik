#include "common.h"
#include "jpeg.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_pixels.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_events.h>

global bool isRunning = true;
global int fd;
global int Width = 640;
global int Height = 480;
global int filter = 0;
global int FilterNums = 9; 

struct Offscreen_buffer {
     SDL_Texture *Texture;
     void *BitmapMemory;
     void *YUYVArray;
     int BitmapHeight;
     int BitmapWidth;
     int BytesPerPixel = 4;
};

global Offscreen_buffer Buffer = {
    .BytesPerPixel = 4
};

enum FILTERS {
    NORMAL = 0,
    MASK,
    PAINT,
    SEPIA,
    GHOST,
    EDGE,
    EDGE_EX,
    MIRROR,
    SOMETHING
};
typedef struct RGB {
    double R;
    double G;
    double B;
} RGB;

RGB YUV_TO_RGB(int Y, int U, int V){
    double C = 1.164f * (Y - 16);
    double D = U - 128;
    double E = V - 128;
    RGB rgb {
        .R = C + (1.596 * E),
        .G = C - (0.392 * D) - (0.813 * E),
        .B = C + (2.017 * D)
    };
    if (rgb.R < 0) rgb.R = 0; else if (rgb.R > 255) rgb.R = 255;
    if (rgb.G < 0) rgb.G = 0; else if (rgb.G > 255) rgb.G = 255;
    if (rgb.B < 0) rgb.B = 0; else if (rgb.B > 255) rgb.B = 255;

    return rgb;
};


// Writes pixel colors into the BITMAP buffer;
internal void Render(Offscreen_buffer *Buffer){
    uint8_t *YUYVArrClone = (uint8_t*)Buffer->YUYVArray;
    uint8_t *BitmapMemoryClone = (uint8_t*)Buffer->BitmapMemory;
    int row = -1;
    int col = 0;
    int BytesInRow = Buffer->BitmapWidth * 2;
    for (int yuyvIndex = 0, nthPixel = 0; yuyvIndex < Buffer->BitmapHeight * Buffer->BitmapWidth * 2; yuyvIndex+=4, nthPixel+=2) {
        int Yi = YUYVArrClone[yuyvIndex];
        int Yii = YUYVArrClone[yuyvIndex+2];
        int U = YUYVArrClone[yuyvIndex+1];
        int V = YUYVArrClone[yuyvIndex+3];
        switch (filter) {
            case (NORMAL):{
                break;
            };

            case(MIRROR):{
                if(col >= BytesInRow / 2){
                    int modified_index = (row * BytesInRow) + (BytesInRow/2) - (col - (BytesInRow/2)) - 4;
                    Yii = YUYVArrClone[modified_index];
                    U = YUYVArrClone[modified_index + 1];
                    Yi = YUYVArrClone[modified_index + 2];
                    V = YUYVArrClone[modified_index + 3];
                }
                break;
            };

            case(MASK):{
                Yi = Yi > 80 ? 255 : 0;
                Yii = Yii > 90 ? 255 : 0;
                break;
            }
            case(PAINT):{
                int levels = 20;
                int step = 256 / levels;
                Yi = (Yi / step)*step;
                Yii = (Yii / step)*step;
                break;
            }
            case(GHOST):{
                Yi = 255 - Yi;
                Yii = 255 - Yii;
                break;
            }
            case(SEPIA):{
                Yi  = Yi  >> 1;
                Yii = Yii >> 1;
                break;
            }
            case(EDGE):{
                Yi  = abs(Yi  - Yii) * 4;
                Yii = Yi;
                break;
            }
            case(EDGE_EX):{
                int d = abs(Yi - Yii);
                Yi  = d > 12 ? 255 : 0;
                Yii = Yi;
                break;
            }
            case(SOMETHING):{
                int d = abs(Yi - Yii);
                Yi  = d * 10;
                Yii = Yi;
                break;
            }
        }

        YUYVArrClone[yuyvIndex] = Yi;
        YUYVArrClone[yuyvIndex+2] = Yii;
        YUYVArrClone[yuyvIndex+1] = U;
        YUYVArrClone[yuyvIndex+3] = V;

        RGB RGBi = YUV_TO_RGB(Yi, U, V);
        RGB RGBii = YUV_TO_RGB(Yii, U, V);
        int currentBitmapIndex = nthPixel * 4;
        (BitmapMemoryClone)[currentBitmapIndex] = RGBi.R;
        (BitmapMemoryClone)[currentBitmapIndex+1] = RGBi.G;
        (BitmapMemoryClone)[currentBitmapIndex+2] = RGBi.B;
        (BitmapMemoryClone)[currentBitmapIndex+3] = 00;

        (BitmapMemoryClone)[currentBitmapIndex+4] = RGBii.R;
        (BitmapMemoryClone)[currentBitmapIndex+5] = RGBii.G;
        (BitmapMemoryClone)[currentBitmapIndex+6] = RGBii.B;
        (BitmapMemoryClone)[currentBitmapIndex+7] = 00;

        if(!(yuyvIndex % (Buffer->BitmapWidth * 2))){
            row++;
            col = 0;
        }
        else {
            col+=4;
        }
    }
}

// (re)creates a BITMAP & Texture of given dimension;
internal void SDLResizeTexture(Offscreen_buffer *Buffer, SDL_Renderer *Renderer, int w, int h){
    if(Buffer->BitmapMemory) {
        free(Buffer->BitmapMemory);
    }
    if(Buffer->YUYVArray) {
        free(Buffer->YUYVArray);
    }
    if(Buffer->Texture) {
        SDL_DestroyTexture(Buffer->Texture);
    }
    Buffer->Texture = SDL_CreateTexture(Renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    int BitmapMemorySize = w * h * Buffer->BytesPerPixel;
    Buffer->BitmapWidth = w;
    Buffer->BitmapHeight = h;
    Buffer->YUYVArray = malloc(w*h*2);
    memset(Buffer->YUYVArray, 0, w*h*2);
    Buffer->BitmapMemory = malloc(BitmapMemorySize);
    memset(Buffer->BitmapMemory, 0, BitmapMemorySize);
}

internal void SDLUpdateWindow(Offscreen_buffer *Buffer, SDL_Window *Winodw, SDL_Renderer *Renderer){

    // Puts the BITMAP into Texture;
    SDL_UpdateTexture(Buffer->Texture, 0, Buffer->BitmapMemory, Buffer->BitmapWidth * Buffer->BytesPerPixel); // last argument is pitch (number of bytes in a row)

    // Copy the Texture into the backbuffer;
    SDL_RenderCopy(Renderer, Buffer->Texture, 0, 0); 

    // Swap the Buffers;
    SDL_RenderPresent(Renderer);
}

void SavePicture(Offscreen_buffer *Buffer){
    uint8_t *YUYVArrClone = (uint8_t*)Buffer->YUYVArray;
    local int BytesPerPixel = 2;
    JPEG_BUFFER OUT = init_buffer();
    int prevLumaDC = 0;
    int prevU_DC = 0;
    int prevV_DC = 0;

    ACHuff EOB_LUMA = {0};
    ACHuff EOB_CHROMA = {0};
    ACHuff ZRL_LUMA = {0};
    ACHuff ZRL_CHROMA = {0};
    for (int i = 0; i < 161; i++) {
        ACHuff luma = AC_LUMA_TABLE[i];
        ACHuff chr = AC_CHROMA_TABLE[i];
        switch (luma.rs) {
            case (0x00): EOB_LUMA = luma; break;
            case (0xF0): ZRL_LUMA = luma; break;
        }
        switch (chr.rs) {
            case (0x00): EOB_CHROMA = chr; break;
            case (0xF0): ZRL_CHROMA = chr; break;
        }
    }

    for(int col = 0; col < Buffer->BitmapHeight; col+=8){
        for(int row = 0; row < Buffer->BitmapWidth; row+=8){
            int ChunkY[8][8] = {0};
            int ChunkU[8][8] = {0};
            int ChunkV[8][8] = {0};
            for (int chunk_col = 0; chunk_col < 8; chunk_col++) {
                int start = ((col+chunk_col) * Buffer->BitmapWidth * BytesPerPixel);
                for (int chunk_row = 0; chunk_row < 8; chunk_row+=BytesPerPixel) {
                    int current =  start + ((chunk_row + row) * BytesPerPixel);
                    int Yi = YUYVArrClone[current] -128;
                    int U =  YUYVArrClone[current+1] -128;
                    int Yii = YUYVArrClone[current+2] -128;
                    int V =  YUYVArrClone[current+3] -128;
                    ChunkY[chunk_col][chunk_row] = Yi;
                    ChunkY[chunk_col][chunk_row+1] = Yii;
                    ChunkU[chunk_col][chunk_row] = U;
                    ChunkU[chunk_col][chunk_row+1] = U;
                    ChunkV[chunk_col][chunk_row] = V;
                    ChunkV[chunk_col][chunk_row+1] = V;
                }
            }
            int coeffsY[8][8];
            FDCT_8X8(ChunkY, coeffsY);

            int coeffsU[8][8];
            FDCT_8X8(ChunkU, coeffsU);

            int coeffsV[8][8];
            FDCT_8X8(ChunkV, coeffsV);

            int quant_coeff_Y[8][8];
            Quantize_8X8(coeffsY, quant_coeff_Y, 1);

            int quant_coeff_U[8][8];
            Quantize_8X8(coeffsU, quant_coeff_U, 0);

            int quant_coeff_V[8][8];
            Quantize_8X8(coeffsV, quant_coeff_V, 0);


            int16_t luma_coeff[64];
            ZigZag8x8(quant_coeff_Y, luma_coeff);

            int16_t U_coeff[64];
            ZigZag8x8(quant_coeff_U, U_coeff);

            int16_t V_coeff[64];
            ZigZag8x8(quant_coeff_V, V_coeff);

            EntropyEncode(&OUT, prevLumaDC, luma_coeff, DC_LUMA_CODE, AC_LUMA_TABLE, ZRL_LUMA, EOB_LUMA);
            prevLumaDC = luma_coeff[0];

            EntropyEncode(&OUT, prevU_DC, U_coeff, DC_CHROMA_CODE, AC_CHROMA_TABLE, ZRL_CHROMA, EOB_CHROMA);
            prevU_DC = U_coeff[0];

            EntropyEncode(&OUT, prevV_DC, V_coeff, DC_CHROMA_CODE, AC_CHROMA_TABLE, ZRL_CHROMA, EOB_CHROMA);
            prevV_DC = V_coeff[0];
        }
    }

    finalize_buffer(&OUT);
    time_t now = time(NULL);
    char buff[16];
    snprintf(buff, sizeof(buff), "picture-%li.jpeg", now);
    write_jpeg_file(buff, &OUT, Buffer->BitmapWidth, Buffer->BitmapHeight);
    free(OUT.data);
};

void HandleEvent(Offscreen_buffer *Buffer, SDL_Event *Event){
    switch (Event->type) {
        case SDL_QUIT:
        {
            printf("Quiting-\n");
            isRunning = false;
        } break;

        case SDL_WINDOWEVENT: 
        {
            switch (Event->window.event) {
                case SDL_WINDOWEVENT_SIZE_CHANGED: {
                    // SDL_Window *Window = SDL_GetWindowFromID(Event->window.windowID);
                    // SDL_Renderer *Renderer = SDL_GetRenderer(Window);
                    // SDLResizeTexture(Buffer, Renderer, Event->window.data1, Event->window.data2);
                } break;
                case SDL_WINDOWEVENT_EXPOSED:
                {
                    SDL_Window *Window = SDL_GetWindowFromID(Event->window.windowID);
                    SDL_Renderer *Renderer = SDL_GetRenderer(Window);
                    SDLUpdateWindow(Buffer, Window, Renderer);
                } break;
            }
        } break;
        case SDL_KEYUP:
        case SDL_KEYDOWN: {
            SDL_Keycode Key = Event->key.keysym.sym; 
            SDL_Keycode Pressed = Event->key.state; 
            if(Pressed) switch (Key) {
                case 32:{
                    printf("Clicks!\n");
                    SavePicture(Buffer);
                    break;
                }
                case 110: {
                    filter++;
                    if(filter == FilterNums){
                        filter = 0;
                    }
                    printf("Changing filter to (%d)\n", filter);
                    break;
                }
            }
        };
    }
}

int main(int argc, char *argv[]){
    generate_dc_table(STD_DC_LUMA_NR, STD_DC_LUMA_SYM, DC_LUMA_CODE);
    generate_ac_table(STD_AC_LUMA_NR, STD_AC_LUMA_SYM, AC_LUMA_TABLE);
    generate_dc_table(STD_DC_CHROMA_NR, STD_DC_CHROMA_SYM, DC_CHROMA_CODE);
    generate_ac_table(STD_AC_CHROMA_NR, STD_AC_CHROMA_SYM, AC_CHROMA_TABLE);

    SDL_Init(SDL_INIT_VIDEO);
    fd = open("/dev/video0", O_RDWR);

    SDL_Window *Window;
    Window = SDL_CreateWindow("PIXEL",
                SDL_WINDOWPOS_UNDEFINED,
                SDL_WINDOWPOS_UNDEFINED,
                Width, Height,
                SDL_WINDOW_RESIZABLE
             );
    if(Window){
        SDL_Renderer *Renderer = SDL_CreateRenderer(Window, -1, 0);
        int w, h;
        SDL_GetWindowSize(Window, &w, &h);
        SDLResizeTexture(&Buffer, Renderer, Width, Height);

        struct v4l2_format fmt = {0};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = Width;
        fmt.fmt.pix.height = Height;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;

        // set capture format
        if(ioctl(fd, VIDIOC_S_FMT, &fmt) < 0){
            printf("Error\n");
        }; 

        struct v4l2_requestbuffers req = {0};
        req.count = 1;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        // request buffer pool from kernel
        ioctl(fd, VIDIOC_REQBUFS, &req);

        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = 0;

        //take metadata of first buffer
        ioctl(fd, VIDIOC_QUERYBUF, &buf);
        Buffer.YUYVArray = mmap(
            NULL, 
            buf.length, 
            PROT_READ | PROT_WRITE, 
            MAP_SHARED, fd, buf.m.offset
        );

        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(fd, VIDIOC_STREAMON, &type);

        printf("All Set! <Space> to click. <N> to change filter.\n");
        while(isRunning){
            // gives the buffer to driver to write into
            ioctl(fd, VIDIOC_QBUF, &buf);

            // clicks.
            ioctl(fd, VIDIOC_DQBUF, &buf);

            SDL_Event Event;
            Render(&Buffer);
            SDLUpdateWindow(&Buffer, Window, Renderer);
            while(SDL_PollEvent(&Event)){
                HandleEvent(&Buffer, &Event);
            }
        }
    }
    if(Buffer.BitmapMemory){
        free(Buffer.BitmapMemory);
    }
    return 0;
}
