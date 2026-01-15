#ifndef ELF_H
#define ELF_H

void load_elf_symbols(const char* filename);

uint64_t lookup_elf_symbol(const char* sym_name);

int is_dynamically_linked_binary(const char* filename);

#endif /* !ELF_H */
