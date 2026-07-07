#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memoryContext.h" // uffsllocType, uffslloc, PERMANENT/TEMPORARY

#ifndef FMACROS
   #include "macros.h" // size, pages, len_db_name_io
#endif

#ifndef FTYPES
  #include "types.h" // tupla, collumm, pageResult, fs_objects, tp_table
#endif

#include "misc.h"
#include "dictionary.h" // tamTupla, leObjetoById
#include "buffer.h"

static int isDeleted(char *linha);
static void writeFrameToDisk(Frame *frame);
static void readFrameFromDisk(Frame *frame);
static void buildFilePath(int tableId, char *path);

// anteriormente BufferPool *pool = NULL;
static BufferManager manager;

void initBufferManager(int frameCount) { // qts quadros vao ficar ativos nessa execuçao
    if (frameCount <= 0 || frameCount > MAX_FRAMES) { // frame deve ser maior q zero e menor q max_frames = 10
        frameCount = DEFAULT_FRAME_COUNT; // se nao, usa o valor padrao de 8 frames
    }

    // tem que alocar em PERMANENT e nao com uffslloc
    // pq o contexto TEMPORARY eh liberado inteiro depois de cada comando sql
    // se alocasse com uffslloc o pool virava lixo de memoria assim que o primeiro comando terminasse
    manager.pool = (BufferPool *) uffsllocType(sizeof(BufferPool), PERMANENT);

    if (manager.pool == NULL) {
        printf("ERROR: falha ao alocar o buffer pool.\n\n");
        exit(1);
    }

    manager.pool->activeFrameCount = frameCount; // qt dos 10 quadros sao usados
    manager.pool->clockHand = 0; // posiciona o ponteiro do clock no primeiro quadro
    manager.pageSize = SIZE;
    manager.diskReads = 0;
    manager.diskWrites = 0;

    for (int i = 0; i < MAX_FRAMES; i++) { // zera todos os 10 quadros do array
        manager.pool->frames[i].blockId = -1; // -1 quadro vazio/disponivel
        manager.pool->frames[i].tableId = -1; // -1 quadro vazio/disponivel
        manager.pool->frames[i].dirty = 0;
        manager.pool->frames[i].pinCount = 0;
        manager.pool->frames[i].referenced = 0;
        manager.pool->frames[i].page.recordCount = 0;
        manager.pool->frames[i].page.usedBytes = 0;
    }
    // mensagem padrao sempre que se conecta ao banco, quadro ativos e tam da pag
    printf("Buffer Manager iniciado com %d quadros de %d bytes.\n", frameCount, SIZE);
}
// para montar o caminho fisico da tabela
static void buildFilePath(int tableId, char *path) {
    struct fs_objects objeto = leObjetoById(tableId);
    strcpy(path, connected.db_directory); // copy o diretorio do bd conectado
    strcat(path, objeto.nArquivo); // concatena o nome do arq
}
// carrega a pag fisica para dentro do frame
static void readFrameFromDisk(Frame *frame) { // recebe o pont do frame q deve ter o tableid
    char path[LEN_DB_NAME_IO];
    buildFilePath(frame->tableId, path);

    FILE *file = fopen(path, "r+b");
    if (file == NULL) {
        // arquivo novo sem nada gravado ainda acontece no primeiro insert de uma tabela deixa a pagina zerado
        frame->page.recordCount = 0;
        frame->page.usedBytes = 0;
        return;
    }
    // calcular deslocamento
    // cada pag logica ocupa sizeof(DiskPage) bytes
    // (1032 bytes: 4 de recordCount + 4 de usedBytes + 1024 de data)
    long int pos = (long int) frame->blockId * sizeof(DiskPage); // começa no blockid x 1032 de data
    fseek(file, pos, SEEK_SET);
    fread(&(frame->page), sizeof(DiskPage), 1, file);
    fclose(file);

    manager.diskReads++;
}

// grava pag fisica a partir do frame no disco
static void writeFrameToDisk(Frame *frame) {
    char path[LEN_DB_NAME_IO]; // monta o caminho
    buildFilePath(frame->tableId, path);

    FILE *file = fopen(path, "r+b");
    if (file == NULL) {
        printf("ERROR: nao foi possivel abrir o arquivo pra gravar a pagina.\n"); // erro fatal, n grava nada
        return;
    }
    // calculo do deslocamento, grava apenas o conteudo logico da pag e os metadados nunca tocam o disco
    long int pos = (long int) frame->blockId * sizeof(DiskPage);
    fseek(file, pos, SEEK_SET);
    fwrite(&(frame->page), sizeof(DiskPage), 1, file);
    fclose(file);

    frame->dirty = 0; // versao gravad em disco esta igual a de memoria entao zera
    manager.diskWrites++; // contador de escritas
}
// para pedir uma pag e pinar
Frame *pinPage(unsigned int blockId, int tableId) { // qual pag de qual tabela
    BufferPool *pool = manager.pool; // atalho local do pool evira os milhares de manager.pool->
    // cache hit, percorre os quadros ativos comparando com a pag e tabela solicitado
    for (int i = 0; i < pool->activeFrameCount; i++) {
        if (pool->frames[i].blockId == (int) blockId && pool->frames[i].tableId == tableId) {
            pool->frames[i].pinCount++; // mais um user ativo nesse quadro
            pool->frames[i].referenced = 1; // proteçao de ser vitima do clock
            return &(pool->frames[i]); // retorna sem precisar ir a disco, win win
        }
    }
    // cache miss, busca quadro livre
    for (int i = 0; i < pool->activeFrameCount; i++) {
        if (pool->frames[i].blockId == -1) { // procura o primeiro quadro vazio (-1)
            pool->frames[i].blockId = blockId; // ocupa esse quadro
            pool->frames[i].tableId = tableId;
            pool->frames[i].dirty = 0;
            pool->frames[i].pinCount = 1; // primeiro user
            pool->frames[i].referenced = 1; // protege de ser vitima do clock
            readFrameFromDisk(&(pool->frames[i])); // traz o conteudo do disco
            return &(pool->frames[i]);
        }
    }
    // sem quadro livre, pool cheio
    // usa segunda chance o clockHand roda em circulo pelos quadro
    // se o quadro ta pinado so pula
    // se o bit de referencia ta ligado desliga o bit
    // e da mais uma chance sem virar vitima ainda
    // se o bit ja ta desligado e sem pino essa vira a vitima
    // maxVoltas eh so pra nao ficar girando pra sempre no caso raro de tudo estar pinado
    int voltas = 0; // contador de volta do hand
    int maxVoltas = 2 * pool->activeFrameCount; // duas voltas completas pelos quadros ativos

    while (voltas < maxVoltas) {
        Frame *atual = &(pool->frames[pool->clockHand]);
        // nem confiro o pincout = 1 porque nao tocamos em pag pinada
        if (atual->pinCount == 0) { // sem user ativo
            if (atual->referenced) { // se n esta pinado, mas foi usada recentecemnte
                atual->referenced = 0; // apenas zera (segunda chance)
            } else { // se o referenced ja era 0, vira vitima da troca
                int victim = pool->clockHand; // salva o indice da vitima
                pool->clockHand = (pool->clockHand + 1) % pool->activeFrameCount; // avança

                if (pool->frames[victim].dirty) { // antes de descartar, verifica se esta suja
                    writeFrameToDisk(&(pool->frames[victim])); // se sim, grava em disco a alteracao
                }
                // reaproveita o quadro para a nova pag
                pool->frames[victim].blockId = blockId;
                pool->frames[victim].tableId = tableId;
                pool->frames[victim].dirty = 0;
                pool->frames[victim].pinCount = 1;
                pool->frames[victim].referenced = 1;
                readFrameFromDisk(&(pool->frames[victim]));

                return &(pool->frames[victim]);
            }
        }

        pool->clockHand = (pool->clockHand + 1) % pool->activeFrameCount; // avança o clock
        voltas++; // incrementa o contador de voltas do ponteiro do clock
    }
    // todos os quadro ativos estavam pinados nas duas voltas do clock
    printf("ERROR: buffer pool cheio, todas as paginas estao em uso.\n");
    return NULL;
}
// devolve a pagina/despina a pag
void unpinPage(Frame *frame) {
    if (frame == NULL) return;
    if (frame->pinCount > 0) frame->pinCount--; // descrementa e despina
}
// grava tudo q esta sujo/alterado na pool no disco
// chamada em parser.c ao final de cada comando
void flushBufferPool() {
    BufferPool *pool = manager.pool;
    for (int i = 0; i < pool->activeFrameCount; i++) { // percorre todos os quadros ativos
        if (pool->frames[i].dirty) { // para todos os alterados que destoam da versao em disco
            writeFrameToDisk(&(pool->frames[i])); // sincroniza conteudo em disco
        } // nao preciso zerar o dirty aqui porque ja eh feito em writeFrameToDisk
    }
}
//para decodificar a pag inteira em tuplas utilizaveis porque o diskpage.data eh um array cru de bytes
// vai receber schema da tabela, metadados e o número da página
PageResult *getPage(tp_table *campos, struct fs_objects objeto, int page) {
    if (page >= PAGES || page < 0) return ERRO_PAGINA_INVALIDA; // PAGES eh o max de pag por tabela
    // pede a pag emprestada ao bm
    Frame *frame = pinPage((unsigned int) page, objeto.cod);
    if (frame == NULL) return ERRO_PAGINA_INVALIDA;

    DiskPage *pagina = &(frame->page); // guarda um atalho pag apontando para o conteudo no frame
    // aloca um array de struct tupla do tama do recordCount q inclui tuplas já deletadas, entao pode ficar maior q tuplas vivas
    tupla *tuplas = (tupla *) uffslloc(sizeof(tupla) * (pagina->recordCount));
    if (!tuplas) {
        unpinPage(frame);
        return ERRO_DE_ALOCACAO;
    }
    // pra contar as tuplas vivas decodificadas , i eh o curso de read dentro do data
    int indiceTupla = 0, i = 0;
    if (!pagina->usedBytes) { // pag vazia
        unpinPage(frame);
        return NULL;
    }
    // buffer temp, um byte por coluna da table
    // 1=coluna nula 0=valor presente
    // basicamente um mapa de nulos de cada tupla lida
    char *nullos = (char *) uffslloc(objeto.qtdCampos * sizeof(char));

    while (i < pagina->usedBytes) { // percorre o data byte a byte
        if (isDeleted(pagina->data + i)) { // se deletado, pula
            i += tamTupla(campos, objeto);
            continue;
        } // se achar tupla viva, grava a posicao (rearooveta pro delete update)
        tuplas[indiceTupla].offset = i;
        tuplas[indiceTupla].ncols = objeto.qtdCampos;
        i++;
        memcpy(nullos, pagina->data + i, objeto.qtdCampos); // copy o mapa de nulos para o buffer temp
        i += objeto.qtdCampos;

        tuplas[indiceTupla].column = (column *) uffslloc(sizeof(column) * objeto.qtdCampos);
        tuplas[indiceTupla].bufferPage = page;
        for (int ic = 0; ic < objeto.qtdCampos; ic++) {
            column *c = &tuplas[indiceTupla].column[ic];
            c->tipoCampo = campos[ic].tipo;
            strcpy(c->nomeCampo, campos[ic].nome);
            if (nullos[ic]) c->valorCampo = COLUNA_NULL;
            else {
                c->valorCampo = (char *) uffslloc(sizeof(char) * campos[ic].tam + 1);
                memcpy(c->valorCampo, pagina->data + i, campos[ic].tam);
                c->valorCampo[campos[ic].tam] = '\0';
            }
            i += campos[ic].tam;
        }
        indiceTupla++;
    }

    PageResult *pg = (PageResult *) uffslloc(sizeof(PageResult));
    pg->tuplas = tuplas;
    pg->nrec = indiceTupla;

    unpinPage(frame);
    return pg;
}

void cria_campo(int tam, int header, char *val, int x) {
    int i;
    char aux[30];
    if (header) {
        for (i = 0; i <= 30 && val[i] != '\0'; i++) aux[i] = val[i];
        for (; i < 30; i++) aux[i] = ' ';
        aux[i] = '\0';
        printf("%s", aux);
        return;
    }
    for (i = 0; i < x; i++) printf(" ");
}

static int isDeleted(char *linha) {
    return linha[0];
}

void addColumn(column **colList, column *c) {
    c->next = NULL;
    if (*colList == NULL) {
        *colList = c;
        return;
    }
    column *t = *colList;
    while (t->next != NULL) t = t->next;
    t->next = c;
}

/*
codigo antigo de antes do buffer manager
usava a struct tp_buffer de types.h e abria fechava o arquivo a cada chamada sem cache nenhum n
ninguem mais chama essas funcao depois da migracao pro buffer manager novo pinPage unpinPage flushBufferPool

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
*/
