#ifndef FBUFFER
#define FBUFFER 1

#include <stdint.h>

#ifndef FMACROS
   #include "macros.h"
#endif

#ifndef FTYPES
  #include "types.h"
#endif

// pool so aceita ate uns 10 quadro pq o alocador de memoria do projeto uffsllocType nao deixa alocar mais que 16kb de uma vez e o bufferpool inteiro tem que caber nesse limite
#define MAX_FRAMES 10

// qtd usada quando ninguem escolhe outra na carga do sgbd isso eh o configuravel na carga pedido no trabalho
#define DEFAULT_FRAME_COUNT 8

// separa o que vai pro disco do que fica so em memoria nada de dirty bit ou pin aqui
typedef struct DiskPage {
    unsigned int recordCount;
    uint32_t usedBytes;
    char data[SIZE];
} DiskPage;

typedef struct Frame {
    int blockId;              // -1 quando o quadro ta vazio
    int tableId;                // -1 quando vazio, cada tabela tem seu proprio id objeto cod
    unsigned char dirty;
    unsigned char pinCount;
    unsigned char referenced;    // bit de segunda chance do algoritmo do relogio
    DiskPage page;
} Frame;

typedef struct {
    Frame frames[MAX_FRAMES];
    int activeFrameCount;   // qtd de quadro realmente configurada pode ser menor que MAX_FRAMES
    int clockHand;            // posicao atual do ponteiro do relogio
} BufferPool;

typedef struct {
    BufferPool *pool;
    int pageSize;
    int diskReads;
    int diskWrites;
} BufferManager;

void initBufferManager(int frameCount);

// depois de usar tem que chamar unpinPage se esquecer o pool enche de pino e trava
Frame *pinPage(unsigned int blockId, int tableId);

void unpinPage(Frame *frame);

void flushBufferPool();

PageResult *getPage(tp_table *campos, struct fs_objects objeto, int page);

// funcao de exibicao usada em misc.c sem relacao com o buffer manager mantida sem mudar nome
void cria_campo(int, int, char *, int);

void addColumn(column **colList, column *c);

// codigo antigo mantido comentado so de referencia ninguem chama mais depois da migracao pro buffer manager
/*
int printbufferpoll(tp_buffer *buffpoll, tp_table *s,struct fs_objects objeto, int num_page);
int colocaTuplaBuffer(tp_buffer *buffer, int from, tp_table *campos, struct fs_objects objeto);
tp_buffer *getBlock(unsigned int id, char* filename);
tp_buffer* initBuffer(unsigned int id);
column * excluirTuplaBuffer(tp_buffer *buffer, tp_table *campos, struct fs_objects objeto, int page, int nTupla);
char *getTupla(tp_table *campos,struct fs_objects objeto, int from);
void setTupla(tp_buffer *buffer,char *tupla, int tam, int pos);
int writeBufferToDisk(tp_buffer *bufferpool, struct fs_objects *objeto);
*/

#endif
