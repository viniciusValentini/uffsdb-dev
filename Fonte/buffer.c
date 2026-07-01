#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> // TRECHO ADICIONADO --> Biblioteca para medir o tempo gasto em operações de buffer;
#include "memoryContext.h"

#ifndef FMACROS // garante que macros.h não seja reincluída
   #include "macros.h"
#endif
///
#ifndef FTYPES // garante que types.h não seja reincluída
  #include "types.h"
#endif

#include "misc.h"
#include "dictionary.h"
#include "buffer.h" // TRECHO ADICIONADO --> Inclusão do header do buffer para acessar suas funções;

static int isDeleted(char *linha);

BufferPool *pool = NULL; // TRECHO ADICIONADO --> Declaração do pool de buffers como variável global;

// NOVA INICIALIZAÇÃO DO BUFFER POOL:
void initBufferPool() {
    pool = (BufferPool*) uffslloc(sizeof(BufferPool)); // TRECHO ADICIONADO --> Alocação do pool de buffers;
    if (pool == NULL) {
        printf("ERROR: Buffer Pool allocation failed.\n\n"); // TRECHO ADICIONADO --> Verificação de alocação do pool de buffers;
        exit(1); // TRECHO ADICIONADO --> Encerramento do programa em caso de falha na alocação do pool de buffers;
    }

    for (int i = 0; i < MAX_BUFFERS; i++) {
        pool->frames[i].id = -1; // TRECHO ADICIONADO --> Inicialização do identificador do buffer como -1 para indicar que está vazio;
        pool->frames[i].tabela_id = -1; // TRECHO ADICIONADO --> Inicialização do identificador da tabela como -1 para indicar que está vazio;
        pool->frames[i].db = 0; // TRECHO ADICIONADO --> Inicialização do identificador da base de dados como 0 para indicar que está vazio;
        pool->frames[i].pc = 0; // TRECHO ADICIONADO --> Inicialização do identificador do programa como 0 para indicar que está vazio;
        pool->frames[i].block.nrec = 0; // TRECHO ADICIONADO --> Inicialização do número de registros como 0 para indicar que está vazio;
        pool->frames[i].block.position = 0; // TRECHO ADICIONADO --> Inicialização da posição como 0 para indicar que está vazio;
    }
    srand(time(NULL)); // TRECHO ADICIONADO --> Inicialização da semente para geração de números aleatórios;
}

void flushFrame(int frame_id) { // TRECHO ADICIONADO --> Função para descarregar o frame especificado no disco;
    // Lógica para obter o nome da tabela pelo tabela_id e dar fwrite
    // no deslocamento (id * sizeof(tp_block))
    printf("Gravando bloco %u da tabela %d no disco...\n", 
            pool->frames[frame_id].id, pool->frames[frame_id].tabela_id); // TRECHO ADICIONADO --> Exibe mensagem de gravação do frame;
    pool->frames[frame_id].db = 0; // TRECHO ADICIONADO --> Reseta o bit de sujeira (dirty bit) após salvar;
}

// NOVO GERENCIADOR DE PÁGINAS:
tp_frame* get_buffer_page(unsigned int block_id, int tabela_id) { // TRECHO ADICIONADO --> Função para recuperar uma página do buffer;
    if (pool == NULL) { // TRECHO ADICIONADO --> Verificação se o pool de buffers foi inicializado;
        initBufferPool(); // TRECHO ADICIONADO --> Inicialização do pool de buffers caso ainda não tenha sido feita;
    }

    // 1. BUFFER HIT:
    for (int i = 0; i < MAX_BUFFERS; i++) {
        if (pool->frames[i].id == block_id && pool->frames[i].tabela_id == tabela_id) {
            pool->frames[i].pc++;
            return &(pool->frames[i]);
        }
    }

    char directory[LEN_DB_NAME_IO];
    strcpy(directory, connected.db_directory);

    // 2. BUFFER MISS:
    for (int i = 0; i < MAX_BUFFERS; i++) {
        if (pool->frames[i].id == -1) {
            FILE *fd = fopen(directory, "r+b");
            if (fd) {
                long int pos = (long int)block_id * sizeof(tp_block);
                fseek(fd, pos, SEEK_SET);
                fread(&(pool->frames[i].block), sizeof(tp_block), 1, fd); 
                fclose(fd);
            }

            pool->frames[i].id = block_id;
            pool->frames[i].tabela_id = tabela_id;
            pool->frames[i].pc = 1;
            pool->frames[i].db = 0;
            return &(pool->frames[i]);
        }
    }

    // 3. Buffer lotado: Substituição Aleatória (conforme solicitado)
    int vitima; // TRECHO ADICIONADO --> Declaração da variável para armazenar o índice do frame vítima escolhido;
    int tentativas = 0; // TRECHO ADICIONADO --> Contador de tentativas de busca por uma vítima elegível;
    do {
        vitima = rand() % MAX_BUFFERS; // TRECHO ADICIONADO --> Sorteia aleatoriamente um frame candidato a vítima;
        tentativas++; // TRECHO ADICIONADO --> Incrementa o número de tentativas realizadas;
    } while(pool->frames[vitima].pc > 0 && tentativas < MAX_BUFFERS); // TRECHO ADICIONADO --> Continua sorteando se o frame estiver pinado e houver tentativas;

    if(pool->frames[vitima].pc > 0) { // TRECHO ADICIONADO --> Verifica se todos os blocos disponíveis estão favoritados;
        printf("Erro: Todos os blocos estao favoritados (pinned)!\n"); // TRECHO ADICIONADO --> Exibe erro caso nenhum bloco possa ser substituído;
        return NULL; // TRECHO ADICIONADO --> Retorna NULL em caso de falha por buffer totalmente pinado;
    }

    // Se a vítima estiver "suja", salva no disco antes de sobrescrever
    if(pool->frames[vitima].db == 1) { // TRECHO ADICIONADO --> Verifica se o bloco foi modificado (dirty bit ativo);
        flushFrame(vitima); // TRECHO ADICIONADO --> Chama a função flushFrame para salvar as alterações em disco;
    }

    // Atualiza o frame com os novos dados
    pool->frames[vitima].id = block_id; // TRECHO ADICIONADO --> Atualiza o identificador do bloco no frame escolhido;
    pool->frames[vitima].tabela_id = tabela_id; // TRECHO ADICIONADO --> Atualiza o identificador da tabela no frame escolhido;
    pool->frames[vitima].db = 0; // TRECHO ADICIONADO --> Reseta o dirty bit do novo bloco carregado;
    pool->frames[vitima].pc = 1; // TRECHO ADICIONADO --> Define o pin count inicial do novo bloco como 1;
    
    return &(pool->frames[vitima]); // TRECHO ADICIONADO --> Retorna o ponteiro para o frame da vítima atualizado;
}

//// imprime os dados no buffer (deprecated?)
int printbufferpoll(tp_buffer *buffpoll, tp_table *s, struct fs_objects objeto, int num_page){
    int aux, i, num_reg = objeto.qtdCampos;

    if(buffpoll[num_page].nrec == 0){
        return ERRO_IMPRESSAO;
    }

    i = aux = 0;
    aux = cabecalho(s, num_reg);
    while(i < buffpoll[num_page].nrec){ 
        drawline(buffpoll, s, objeto, i, num_page);
        i++;
    }
    return SUCCESS;
}

tp_buffer* initBuffer(unsigned int id){
    tp_buffer *buffer = uffslloc(sizeof(tp_buffer));

    if (buffer == NULL) {
        printf("ERROR: Memory allocation failed.\n\n");
        return NULL;
    }

    buffer->id = id;
    return buffer;
}

tp_buffer *getBlock(unsigned int id, char* filename){
    FILE *fd = fopen(filename, "r+");
    if (!fd) {
        printf("ERROR: failed to open %s", filename);
        return NULL;
    }

    long int pos = (long int)id * sizeof(tp_buffer);
    fseek(fd, pos, SEEK_SET);
    tp_buffer* buffer = uffslloc(sizeof(tp_buffer));
    fread(buffer, sizeof(tp_buffer), 1, fd);
    fclose(fd);
    return buffer;
}

// RETORNA PAGINA DO BUFFER
PageResult *getPage(tp_table *campos, struct fs_objects objeto, int page){
    if(page >= PAGES || page < 0) return ERRO_PAGINA_INVALIDA;

    char directory[LEN_DB_NAME_IO];
    strcpy(directory, connected.db_directory);
    strcat(directory, objeto.nArquivo);

    tp_buffer *buffer = getBlock((unsigned int) page, directory);
    tupla *tuplas = (tupla *)uffslloc(sizeof(tupla) * (buffer->nrec)); 

    if(!tuplas)
        return ERRO_DE_ALOCACAO;

    int indiceTupla = 0, i = 0;
    if (!buffer->position)
        return NULL;

    char* nullos = (char *)uffslloc(objeto.qtdCampos * sizeof(char));

    while(i < buffer->position){
        if(isDeleted(buffer->data + i)) {
            i += tamTupla(campos, objeto);
            continue;
        }
        tuplas[indiceTupla].offset = i; 
        tuplas[indiceTupla].ncols = objeto.qtdCampos;
        i++; 
        memcpy(nullos, buffer->data + i, objeto.qtdCampos);
        i += objeto.qtdCampos;

        tuplas[indiceTupla].column = (column *)uffslloc(sizeof(column) * objeto.qtdCampos);
        tuplas[indiceTupla].bufferPage = page;
        for (int ic = 0; ic < objeto.qtdCampos; ic++){
            column *c = &tuplas[indiceTupla].column[ic];
            c->tipoCampo = campos[ic].tipo;
            strcpy(c->nomeCampo, campos[ic].nome); 
            if(nullos[ic]) c->valorCampo = COLUNA_NULL;
            else {
                c->valorCampo = (char *)uffslloc(sizeof(char) * campos[ic].tam + 1);
                memcpy(c->valorCampo, buffer->data + i, campos[ic].tam);
                c->valorCampo[campos[ic].tam] = '\0';
            }
            i += campos[ic].tam;
        }
        indiceTupla++;
    }
    PageResult *pg = (PageResult *)uffslloc(sizeof(PageResult));
    pg->tuplas = tuplas;
    pg->nrec = indiceTupla;

    return pg; 
}

// EXCLUIR TUPLA BUFFER
column * excluirTuplaBuffer(tp_buffer *buffer, tp_table *campos, struct fs_objects objeto, int page, int nTupla){
    column *tuplas = (column *)uffslloc(sizeof(column)*objeto.qtdCampos);

    if(tuplas == NULL)
        return ERRO_DE_ALOCACAO;

    if(buffer[page].nrec == 0) 
        return ERRO_PARAMETRO;

    int i, tamTpl = tamTupla(campos, objeto), j = 0, t = 0;
    i = tamTpl * nTupla; 

    while(i < tamTpl * nTupla + tamTpl){
        t = 0;
        tuplas[j].valorCampo = (char *)uffslloc(sizeof(char)*campos[j].tam); 
        tuplas[j].tipoCampo = campos[j].tipo;  
        strcpylower(tuplas[j].nomeCampo, campos[j].nome);   

        while(t < campos[j].tam){
            tuplas[j].valorCampo[t] = buffer[page].data[i];    
            t++;
            i++;
        }
        j++;
    }
    j = i;
    i = tamTpl * nTupla;
    for(; i < buffer[page].position; i++, j++) 
        buffer[page].data[i] = buffer[page].data[j];

    buffer[page].position -= tamTpl;
    buffer[page].nrec--;

    return tuplas; 
}

// INSERE UMA TUPLA NO BUFFER!
char *getTupla(tp_table *campos, struct fs_objects objeto, int from){ 
    int tamTpl = tamTupla(campos, objeto); 
    char *linha = (char *)uffslloc(sizeof(char)*tamTpl);

    FILE *dados;
    from = from * tamTpl;
    char directory[LEN_DB_NAME_IO];
    strcpy(directory, connected.db_directory);
    strcat(directory, objeto.nArquivo);

    dados = fopen(directory, "r");
    if (dados == NULL) {
        return ERRO_DE_LEITURA;
    }

    fseek(dados, from, SEEK_CUR);
    if(fgetc (dados) == EOF){
        fclose(dados);
        return ERRO_DE_LEITURA;
    }
    
    fseek(dados, -1, SEEK_CUR);
    fread(linha, sizeof(char), tamTpl, dados); 

    fclose(dados);
    return linha;
}

void setTupla(tp_buffer *buffer, char *tupla, int tam, int pos) { 
  int i = buffer[pos].position;
  for (; i < buffer[pos].position + tam; i++)
    buffer[pos].data[i] = *(tupla++);
}

//// insere uma tupla no buffer
int colocaTuplaBuffer(tp_buffer *buffer, int from, tp_table *campos, struct fs_objects objeto){
    int i, found;
    char *tupla = getTupla(campos, objeto, from);
    if(tupla == ERRO_DE_LEITURA)  return ERRO_LEITURA_DADOS;

    int tam = tamTupla(campos, objeto);

    for(i = found = 0; !found && i < PAGES; i++) {
        if(SIZE - buffer[i].position > tam) {
            setTupla(buffer, tupla, tam, i);
            found = 1;
            buffer[i].position += tam; 
            if(isDeleted(tupla)) {
                return ERRO_LEITURA_DADOS_DELETADOS;
            }
             buffer[i].nrec++;
        }
    }
    return found ? SUCCESS : ERRO_BUFFER_CHEIO;
}

void cria_campo(int tam, int header, char *val, int x) {
  int i;
  char aux[30];
  if(header){
    for(i = 0; i <= 30 && val[i] != '\0'; i++) aux[i] = val[i];
    for(;i < 30;i++) aux[i] = ' ';
    aux[i] ='\0';
    printf("%s", aux);
    return;
  }
  for(i = 0; i < x; i++) printf(" ");
}

int writeBufferToDisk(tp_buffer *buffer, struct fs_objects *objeto) {
    int success = 1; 
    char directory[LEN_DB_NAME_IO];
    strcpy(directory, connected.db_directory);
    strcat(directory, objeto->nArquivo);

    FILE *dados = fopen(directory, "r+b");
    if (!dados) {
        printf("ERROR: Unable to open file for writing.\n");
        return 0;
    }
    
    fseek(dados, buffer->id * sizeof(tp_buffer), SEEK_SET);
    buffer->db = 0;
    buffer->pc = 0;
    fwrite(buffer, sizeof(tp_buffer), 1, dados);
    fclose(dados);

    return success;
}

static int isDeleted(char *linha){
    return linha[0]; 
}

void addColumn(column **colList, column *c){
    c->next = NULL;
    if(*colList == NULL) {
        *colList = c;
        return;
    }
    column *t = *colList;
    while(t->next != NULL) t = t->next;
    t->next = c;
}
