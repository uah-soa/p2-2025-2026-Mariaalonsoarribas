/*
    Copyright 2023 The Operating System Group at the UAH
    sim_pag_f2ch.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "./sim_paging.h"

// Function that initialises the tables

void init_tables(ssystem* S) {
  int i;

  // Reset pages
  memset(S->pgt, 0, sizeof(spage) * S->numpags);

  // Empty LRU stack
  S->lru = -1;

  // Reset LRU(t) time
  S->clock = 0;

  // Circular list of free frames
  for (i = 0; i < S->numframes - 1; i++) {
    S->frt[i].page = -1;
    S->frt[i].next = i + 1;
  }

  S->frt[i].page = -1;  // Now i == numframes-1
  S->frt[i].next = 0;   // Close circular list
  S->listfree = i;      // Point to the last one

  // Empty circular list of occupied frames
  S->listoccupied = -1;
}

// Functions that simulate the hardware of the MMU

unsigned sim_mmu(ssystem* S, unsigned virtual_addr, char op) {
  unsigned physical_addr = ~0U;   // dirección física por defecto (inválida)
  int page, frame, offset;
  // TODO(student):
  //       Type in the code that simulates the MMU's (hardware)
  //       behaviour in response to a memory access operation
    

  // Calcular página y desplazamiento
  page = virtual_addr / S->pagsz;
  offset = virtual_addr % S->pagsz;

  // Comprobar si el acceso es ilegal
  if (page < 0 || page >= S->numpags) {
    S->numillegalrefs++;
    return ~0U;
  }

  // Si no está presente, fallo de página
  if (!S->pgt[page].present) {
    handle_page_fault(S, virtual_addr);
  }

  // Ahora debe estar presente
  frame = S->pgt[page].frame;

  // Calcular dirección física
  physical_addr = frame * S->pagsz + offset;

  // Registrar referencia (lectura/escritura)
  reference_page(S, page, op);

  // Modo detallado
  if (S->detailed)
    printf("\t%c %u == P %d (F %d) + %d\n", op, virtual_addr, page, frame, offset);

  return physical_addr;
}

void reference_page(ssystem* S, int page, char op) {
    if (op == 'R') {
        S->numrefsread++;
    } else if (op == 'W') {
        S->pgt[page].modified = 1;
        S->numrefswrite++;
    }

    // 2nd chance: marcar como referenciada
    S->pgt[page].referenced = 1;
}


// Functions that simulate the operating system

void handle_page_fault(ssystem* S, unsigned virtual_addr) {
  int page, victim, frame, last;

  // TODO(student):
  //       Type in the code that simulates the Operating
  //       System's response to a page fault trap

  page = virtual_addr / S->pagsz;  // calcular número de página
  S->numpagefaults++;              // incrementar contador de fallos

  if (S->detailed)
        printf("@ PAGE_FAULT in P%d!\n", page);

  // Si hay marcos libres
  if (S->listfree != -1) {
      last = S->listfree;          // índice del último libre
      frame = S->frt[last].next;   // tomar el primer frame libre

      // Si era el último libre
      if (frame == last) {
          S->listfree = -1;
    } else {
          S->frt[last].next = S->frt[frame].next;  // quitar frame de la lista
        }

        // Ocupamos el frame con la página
        occupy_free_frame(S, frame, page);

    } else {
        // No hay libres → elegir víctima según política (random aquí)
        victim = choose_page_to_be_replaced(S);
        replace_page(S, victim, page);
    }
}

/*
static unsigned myrandom(unsigned from,  // <<--- random
                         unsigned size) {
  unsigned n;

  n = from + (unsigned)(rand() / (RAND_MAX + 1.0) * size);

  if (n > from + size - 1)  // These checks shouldn't
    n = from + size - 1;    // be necessary, but it's
  else if (n < from)        // better to not rely too
    n = from;               // much on the floating
                            // point operations
  return n;
}*/

//Actulizes for FIFO 2ch
int choose_page_to_be_replaced(ssystem* S) {
    int victim_frame = S->frt[S->listoccupied].next;
    int victim = S->frt[victim_frame].page;

    // recorre la lista hasta encontrar bit de referencia = 0
    while (S->pgt[victim].referenced) {
        S->pgt[victim].referenced = 0; // limpia bit
        S->listoccupied = victim_frame; // mueve al final
        victim_frame = S->frt[S->listoccupied].next;
        victim = S->frt[victim_frame].page;
    }

    if (S->detailed)
        printf("@ Choosing (FIFO 2nd chance) P%d of F%d to be replaced\n", victim, victim_frame);

    // actualizar lista circular
    S->listoccupied = victim_frame;

    return victim;
}



void replace_page(ssystem* S, int victim, int newpage) {
  int frame;

  frame = S->pgt[victim].frame;

  if (S->pgt[victim].modified) {
    if (S->detailed)
      printf(
          "@ Writing modified P%d back (to disc) to "
          "replace it\n",
          victim);

    S->numpgwriteback++;
  }

  if (S->detailed)
    printf("@ Replacing victim P%d with P%d in F%d\n", victim, newpage, frame);

  S->pgt[victim].present = 0;

  S->pgt[newpage].present = 1;
  S->pgt[newpage].frame = frame;
  S->pgt[newpage].modified = 0;

  S->frt[frame].page = newpage;
}
/*
void occupy_free_frame(ssystem* S, int frame, int page) {
  if (S->detailed) printf("@ Storing P%d in F%d\n", page, frame);

  // TODO(student):
  //       Write the code that links the page with the frame and
  //       vice-versa, and wites the corresponding values in the
  //       state bits of the page (presence...)

  // Enlace frame → página
  S->frt[frame].page = page;

  // Enlace página → frame
  S->pgt[page].present = 1;
  S->pgt[page].frame = frame;
  S->pgt[page].modified = 0;   // limpia bit modificado si existía antes
}*/

void occupy_free_frame(ssystem* S, int frame, int page) {
    if (S->detailed) printf("@ Storing P%d in F%d\n", page, frame);

    // enlaza página con frame
    S->pgt[page].present = 1;
    S->pgt[page].frame = frame;
    S->pgt[page].modified = 0;

    S->frt[frame].page = page;

    // agregar frame a lista ocupada FIFO
    if (S->listoccupied == -1) {
        S->frt[frame].next = frame; // solo elemento, se apunta a sí mismo
    } else {
        S->frt[frame].next = S->frt[S->listoccupied].next;
        S->frt[S->listoccupied].next = frame;
    }
    S->listoccupied = frame;
}

// Functions that show results

void print_page_table(ssystem* S) {
    int p;

    printf("%10s %10s %10s %10s %12s\n",
           "PAGE", "Present", "Frame", "Modified", "Referenced");

    for (p = 0; p < S->numpags; p++) {
        if (S->pgt[p].present)
            printf("%8d   %6d     %8d   %6d     %8d\n",
                   p,
                   S->pgt[p].present,
                   S->pgt[p].frame,
                   S->pgt[p].modified,
                   S->pgt[p].referenced);
        else
            printf("%8d   %6d     %8s   %6s     %8s\n",
                   p,
                   S->pgt[p].present,
                   "-",
                   "-",
                   "-");
    }
}

void print_frames_table(ssystem* S) {
    int p, f;

    printf("%10s %10s %10s %10s %12s\n",
           "FRAME", "Page", "Present", "Modified", "Referenced");

    for (f = 0; f < S->numframes; f++) {
        p = S->frt[f].page;

        if (p == -1) {
            printf("%8d   %8s   %6s     %6s     %8s\n",
                   f, "-", "-", "-", "-");
        }
        else if (S->pgt[p].present) {
            printf("%8d   %8d   %6d     %6d     %8d\n",
                   f,
                   p,
                   S->pgt[p].present,
                   S->pgt[p].modified,
                   S->pgt[p].referenced);
        }
        else {
            printf("%8d   %8d   %6d     %6s   ERROR!\n",
                   f,
                   p,
                   S->pgt[p].present,
                   "-");
        }
    }
}


void print_replacement_report(ssystem* S) {
    printf("---------- INFORME REEMPLAZO (FIFO 2nd CHANCE) ---------\n");
    for(int i=0; i<S->numframes; i++) {
        int p = S->frt[i].page;
        if (p != -1) { // marco ocupado
            printf("Frame %d: Page %d, RefBit=%d\n", i, p, S->pgt[p].referenced);
        } else {
            printf("Frame %d: Empty\n", i);
        }
    }
    printf("-------------------------------------\n");
}

