#ifndef FBUFFER
#define FBUFFER 1 // flag controlar os includes

#include <stdint.h> // TRECHO ADICIONADO

// CORREÇÃO: Evita redefinir SIZE se macros.h já o definiu
#ifndef SIZE
   #define SIZE 4096 // TRECHO ADICIONADO --> Tamanho do bloco;
#endif

#define MAX_BUFFERS 128 // TRECHO ADICIONADO --> Quantidade de páginas no pool;

#ifndef FMACROS // garante que macros.h não seja reincluída
   #include "macros.h"
#endif
//
#ifndef FTYPES // garante que types.h não seja reincluída
  #include "types.h"
#endif

typedef struct tp_block { // TRECHO ADICIONADO --> Estrutura para armazenar um bloco do buffer;
    unsigned int nrec; // TRECHO ADICIONADO --> Número de registros armazenados na página;
    uint32_t position; // TRECHO ADICIONADO --> Número de registro que a página ainda pode receber;
    char data[4096]; // TRECHO ADICIONADO --> Dados (tuplas);
} tp_block; // TRECHO ADICIONADO --> Estrutura para armazenar um bloco do buffer;

// CORREÇÃO: Mudado para tp_frame para não colidir com o types.h
typedef struct tp_frame { 
    unsigned int id; // TRECHO ADICIONADO --> Identificador do buffer;
    int tabela_id; // TRECHO ADICIONADO --> Identificador da tabela que está armazenada no buffer;
    unsigned char db; // TRECHO ADICIONADO --> Dirty bit;
    unsigned char pc; // TRECHO ADICIONADO --> Pin count;
    tp_block block; // TRECHO ADICIONADO --> Bloco do buffer;
} tp_frame; 

typedef struct { // TRECHO ADICIONADO --> Estrutura para armazenar o pool de buffers;
    tp_frame frames[MAX_BUFFERS]; // TRECHO ADICIONADO --> Array de blocos do buffer;
} BufferPool; // TRECHO ADICIONADO --> Estrutura para armazenar o pool de buffers;

// PROTOTIPAÇÃO DAS NOVAS FUNÇÕES:
void initBufferPool(); // TRECHO ADICIONADO --> Função para inicializar o pool de buffers;
tp_frame* get_buffer_page(unsigned int block_id, int tabela_id); // TRECHO ADICIONADO --> Função para recuperar uma página do buffer;
void flushFrame(int frame_id); // TRECHO ADICIONADO --> Função para descarregar o frame especificado no disco;

/*
    Esta função imprime todos os dados carregados numa determinada página do buffer
    *buffer - Estrutura para armazenar tuplas na memória
    *s - Estrutura que armazena esquema da tabela para ler os dados do buffer
    *objeto - Estrutura que armazena dados sobre a tabela que está no buffer
    *num_page - Número da página a ser impressa
*/
int printbufferpoll(tp_buffer *buffpoll, tp_table *s,struct fs_objects objeto, int num_page);
/*
    Esta função insere uma tupla em uma página do buffer em que haja espaço suficiente.
    Retorna ERRO_BUFFER_CHEIO caso não haja espeço para a tupla

    *buffer - Estrutura para armazenar tuplas na meméria
    *from   - Número da tupla a ser posta no buffer. Este número é relativo a ordem de inserção da
              tupla na tabela em disco.
    *campos - Estrutura que armazena esquema da tabela para ler os dados do buffer
    *objeto - Estrutura que armazena dados sobre a tabela que está no buffer
*/
int colocaTuplaBuffer(tp_buffer *buffer, int from, tp_table *campos, struct fs_objects objeto);

/*
    Esta função recebe um arquivo e o id do buffer,
    retorna o buffer carregado ou erro. toma toma.
*/
tp_buffer *getBlock(unsigned int id, char* filename);

/*
    Retorna um buffer iniciado top top. 
*/
tp_buffer* initBuffer(unsigned int id); 

/*
    Esta função recupera uma página do buffer e retorna a mesma em uma estrutura do tipo tupla
    A estrutura column possui informações de como manipular os dados
    *campos - Estrutura que armazena esquema da tabela para ler os dados do buffer
    *objeto - Estrutura que armazena dados sobre a tabela que está no buffer
    *page - Número da página a ser recuperada (0 a PAGES)
*/
PageResult *getPage(tp_table *campos, struct fs_objects objeto, int page); 

/*
    Esta função uma determinada tupla do buffer e retorna a mesma em uma estrutura do tipo column;
    A estrutura column possui informações de como manipular os dados
    *buffer - Estrutura para armazenar tuplas na meméria
    *campos - Estrutura que armazena esquema da tabela para ler os dados do buffer
    *objeto - Estrutura que armazena dados sobre a tabela que está no buffer
    *page   - Número da página a ser recuperada uma tupla (0 a PAGES)
    *nTupla - Número da tupla a ser excluida, este número é relativo a página do buffer e não a
              todos os registros carregados
*/
column * excluirTuplaBuffer(tp_buffer *buffer, tp_table *campos, struct fs_objects objeto, int page, int nTupla);
////
char *getTupla(tp_table *campos,struct fs_objects objeto, int from);

void setTupla(tp_buffer *buffer,char *tupla, int tam, int pos);
////
void cria_campo(int , int , char *, int );

/* ----------------------------------------------------------------------------------------------
    Objetivo:   Utilizada para gravar as mudanças do buffer no disco.
    Parametros: Buffer (tp_buffer) e dados da tabela (fs_objects)
    Retorno:    1 para sucesso, 0 para falha.
   ---------------------------------------------------------------------------------------------*/
int writeBufferToDisk(tp_buffer *bufferpool, struct fs_objects *objeto);

void addColumn(column **colList, column *c);

#endif
