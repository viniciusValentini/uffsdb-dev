#ifndef FBUFFER
#define FBUFFER 1

#include <stdint.h>

#ifndef FMACROS
   #include "macros.h"
#endif

#ifndef FTYPES
  #include "types.h"
#endif

// pool so aceita ate uns 10 quadro pq o alocador de
// memoria do projeto uffsllocType nao deixa alocar
// mais que 16kb de uma vez e o bufferpool inteiro tem que caber nesse limite
#define MAX_FRAMES 10

// qtd usada quando ninguem escolhe outra na carga do sgbd
#define DEFAULT_FRAME_COUNT 8

typedef struct DiskPage { // so o q eh gravado no disco
    unsigned int recordCount;
    uint32_t usedBytes;
    char data[SIZE]; // 1024 bytes
} DiskPage;

// metadado q so existem em memoria
typedef struct Frame {
    int blockId;              // -1 quando o quadro ta vazio
    int tableId;                // -1 quando vazio, cada tabela tem seu proprio id objeto cod
    unsigned char dirty;
    unsigned char pinCount;
    unsigned char referenced;    // bit de segunda chance do algoritmo do relogio
    DiskPage page;
} Frame;

typedef struct {
    Frame frames[MAX_FRAMES]; // max frames de 10
    int activeFrameCount;   // qtd de quadro realmente configurada pode ser menor que MAX_FRAMES
    int clockHand;            // posicao atual do ponteiro do relogio
} BufferPool;

typedef struct { // struct nova do bm, tem o ponteiro da pool e estatisticas de uso
    BufferPool *pool;
    int pageSize;
    int diskReads;
    int diskWrites;
} BufferManager;

// inicia o bm e aloca o pool de quadros em memoria permanete e zera ele
void initBufferManager(int frameCount);

// pede uma pag emprestada cache hit/ pina
// depois de usar tem que chamar unpinPage se esquecer o pool enche de pino e trava
Frame *pinPage(unsigned int blockId, int tableId);

// devolve a pag emprestada/despina
void unpinPage(Frame *frame);

// percorre todos os quadros e graba no disco os modificados (dirty=1)
void flushBufferPool();

// decodificador uma pag em tuplas usaveis pelo sql
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
