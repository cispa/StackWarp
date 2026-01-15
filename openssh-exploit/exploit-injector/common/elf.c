#include <libelf.h>
#include <fcntl.h>
#include <gelf.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "elf.h"

//#define DEBUGMODE

typedef struct {
    const char* sym_name;
    uint64_t sym_addr;
} symbol_entry_t;

symbol_entry_t* symbol_list;
size_t symbol_list_length;
size_t symbol_list_reserved;

void append_to_symbol_list(symbol_entry_t sym) {
    if (symbol_list == NULL) {
        symbol_list_reserved = 32;
        symbol_list = malloc(symbol_list_reserved * sizeof(symbol_entry_t));
    }
    if (symbol_list_reserved < symbol_list_length + 1) {
        symbol_list_reserved = symbol_list_reserved * 2;
        symbol_list = realloc(symbol_list, 
            symbol_list_reserved * sizeof(symbol_entry_t));
        if (symbol_list == NULL) {
            fprintf(stderr, "realloc failed\n");
            exit(1);
        }
    }
    symbol_list[symbol_list_length++] = sym;
}

uint64_t lookup_elf_symbol(const char* sym_name) {
  if (symbol_list == NULL) {
    printf("[!] Load symbols first!\n");
    exit(1);
  }
  // while this is a linear search, the number of ELF symbols is hopefully small
  // enough to make this acceptable. if not -> dict or binsect
  for (size_t i = 0; i < symbol_list_length; i++) {
    if (strcmp(symbol_list[i].sym_name, sym_name) == 0) {
      return symbol_list[i].sym_addr;
    }
  }
  return -1;
}

void load_elf_symbols(const char* filename) {
  if (elf_version(EV_CURRENT) == EV_NONE) {
    fprintf(stderr, "ELF library initialization failed.\n");
    return;
  }

  int fd = open(filename, O_RDONLY, 0);
  if (fd < 0) {
    perror("open");
    return;
  }

  Elf* elf = elf_begin(fd, ELF_C_READ, NULL);
  if (!elf) {
    fprintf(stderr, "elf_begin failed: %s\n", elf_errmsg(-1));
    close(fd);
    return;
  }

  Elf_Scn* section_desc = NULL;
  GElf_Shdr section_header;

  // iterate through sections to find the symbol table
  while ((section_desc = elf_nextscn(elf, section_desc)) != NULL) {
    gelf_getshdr(section_desc, &section_header);
    if (section_header.sh_type == SHT_SYMTAB) {
      // found symbol table
      Elf_Data* section_data = elf_getdata(section_desc, NULL);
      int count = section_header.sh_size / section_header.sh_entsize;
      for (size_t i = 0; i < count; ++i) {
        GElf_Sym sym;
        gelf_getsym(section_data, i, &sym);
        if (sym.st_value != 0) {  // skip symbols without address
          symbol_entry_t entry = {
            .sym_name = elf_strptr(elf, section_header.sh_link, 
                sym.st_name),
            .sym_addr = (uint64_t)sym.st_value};
          append_to_symbol_list(entry);
#ifdef DEBUGMODE
          printf("[*] %s @ %p\n", entry.sym_name, 
              (void*)entry.sym_addr);
#endif
        }
      }
    }
  }

  elf_end(elf);
  close(fd);
}

int is_dynamically_linked_cached_res = -1;

int is_dynamically_linked_binary(const char* filename) {
  if (is_dynamically_linked_cached_res != -1) {
    return is_dynamically_linked_cached_res;
  }

  // Open the binary file
  int fd = open(filename, O_RDONLY);
  if (fd < 0) {
    perror("Error opening ELF file");
    exit(1);
  }

  // Read the ELF header
  Elf64_Ehdr elf_header;
  if (read(fd, &elf_header, sizeof(elf_header)) != sizeof(elf_header)) {
    perror("Error reading ELF header");
    close(fd);
    exit(1);
  }

  // Seek to the program headers
  if (lseek(fd, elf_header.e_phoff, SEEK_SET) != elf_header.e_phoff) {
    perror("Error seeking to program headers");
    close(fd);
    exit(1);
  }

  // Loop through the program headers to check for PT_DYNAMIC
  int has_dynamic_section = 0;
  for (int i = 0; i < elf_header.e_phnum; i++) {
      Elf64_Phdr program_header;
      if (read(fd, &program_header, sizeof(program_header)) != sizeof(program_header)) {
          perror("Error reading program headers");
          close(fd);
          exit(1);
      }

      // Check if the program header type is PT_DYNAMIC
      if (program_header.p_type == PT_DYNAMIC) {
          has_dynamic_section = 1;
          break;
      }
  }
  // store the result in a global variable to avoid re-computation
  is_dynamically_linked_cached_res = has_dynamic_section;

  close(fd);
  return is_dynamically_linked_cached_res;
}
