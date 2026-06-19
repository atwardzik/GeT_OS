//
// Created by Artur Twardzik on 28/05/2026.
//

#include "loader.h"
#include "errno.h"
#include "error.h"
#include "libc.h"
#include "memory.h"
#include "proc.h"
#include "fs/file.h"
#include "fs/ramfs.h"


#include <stdint.h>


constexpr int ELF_NIDENT = 16;
constexpr int E_TYPE_REL = 1;
constexpr int E_TYPE_EXEC = 2;
constexpr int E_TYPE_DYN = 3;
constexpr int E_MACHINE_ARM = 0x28;
constexpr int SST_FUNC = 2;
constexpr int PT_LOAD = 1;
constexpr int STN_UNDEF = 0;

typedef uint16_t HalfWord;
typedef uint32_t Word;
typedef int32_t Sword;
typedef uint32_t Address;
typedef uint32_t Offset;

typedef struct {
        uint8_t e_ident[ELF_NIDENT];
        HalfWord e_type;
        HalfWord e_machine;
        Word e_version;
        Address e_entry;
        Offset e_phoff;
        Offset e_shoff;
        Word e_flags;
        HalfWord e_ehsize;
        HalfWord e_phentsize;
        HalfWord e_phnum;
        HalfWord e_shentsize;
        HalfWord e_shnum;
        HalfWord e_shstrndx; //index of names field in section headers table
} Elf32_EHdr;

typedef struct {
        Word sh_name;
        Word sh_type;
        Word sh_flags;
        Address sh_addr;
        Offset sh_offset;
        uint32_t sh_size;
        Word sh_link;
        Word sh_info;
        Word sh_addralign;
        Word sh_entsize;
} Elf32_SHdr;

typedef struct {
        Word p_type;
        Offset p_offset;
        Address p_vaddr;
        Address p_paddr;
        Word p_filesz;
        Word p_memsz;
        Word p_flags;
        Word p_align;
} Elf32_PHdr;

typedef struct {
        Word st_name;
        Address st_value;
        Word st_size;
        unsigned char st_info;
        unsigned char st_other;
        HalfWord st_shndx;
} Elf32_Sym;

#define ELF32_ST_BIND(i)   ((i)>>4)
#define ELF32_ST_TYPE(i)   ((i)&0xf)
#define ELF32_ST_INFO(b,t) (((b)<<4)+((t)&0xf))

typedef struct {
        Address r_offset;
        Word r_info;
} Elf32_Rel;

#define ELF32_R_SYM(i) ((i)>>8)
#define ELF32_R_TYPE(i) ((unsigned char)(i))
#define ELF32_R_INFO(s,t) (((s)<<8)+(unsigned char)(t))


typedef struct {
        Sword d_tag;

        union {
                Word d_val;
                Address d_ptr;
        } d_un;
} Elf32_Dyn;

typedef enum {
        DT_NULL            = 0,
        DT_NEEDED          = 1,
        DT_PLTRELSZ        = 2,
        DT_PLTGOT          = 3,
        DT_HASH            = 4,
        DT_STRTAB          = 5,
        DT_SYMTAB          = 6,
        DT_RELA            = 7,
        DT_RELASZ          = 8,
        DT_RELAENT         = 9,
        DT_STRSZ           = 10,
        DT_SYMENT          = 11,
        DT_INIT            = 12,
        DT_FINI            = 13,
        DT_SONAME          = 14,
        DT_RPATH           = 15,
        DT_SYMBOLIC        = 16,
        DT_REL             = 17,
        DT_RELSZ           = 18,
        DT_RELENT          = 19,
        DT_PLTREL          = 20,
        DT_DEBUG           = 21,
        DT_TEXTREL         = 22,
        DT_JMPREL          = 23,
        DT_BIND_NOW        = 24,
        DT_INIT_ARRAY      = 25,
        DT_FINI_ARRAY      = 26,
        DT_INIT_ARRAYSZ    = 27,
        DT_FINI_ARRAYSZ    = 28,
        DT_RUNPATH         = 29,
        DT_FLAGS           = 30,
        DT_ENCODING        = 32,
        DT_PREINIT_ARRAY   = 32,
        DT_PREINIT_ARRAYSZ = 33,
        DT_SYMTAB_SHNDX    = 34,
        DT_RELRSZ          = 35,
        DT_RELR            = 36,
        DT_RELRENT         = 37,
        DT_SYMTABSZ        = 39,

        DT_LOOS   = 0x6000000D,
        DT_HIOS   = 0x6FFFF000,
        DT_LOPROC = 0x70000000
} ElfDynamicTag;


typedef struct {
        /*INFO*/


        const char *interp;

        const Elf32_Sym *symtab;
        size_t symtab_len;

        const char *strtab;

        const Elf32_Sym *dynsym;
        size_t dynsym_len;

        const char *dynstr;

        const Elf32_Rel *rel_plt;
        size_t rel_plt_len;

        const Elf32_Rel *rel_dyn;
        size_t rel_dyn_len;

        const Elf32_Rel *rel_data;
        size_t rel_data_len;

        const Elf32_Rel *rel_bss;
        size_t rel_bss_len;

        const Address *got;
        size_t got_len;

        const Elf32_Dyn *dynamic;
        size_t dynamic_len;

        const Address *got_plt;
        size_t got_plt_len;

        const Word *hash;
        size_t hash_len;

        /*EXEC*/


        const uint8_t *text;
        size_t text_len;

        const uint8_t *rodata;
        size_t rodata_len;

        const uint8_t *data;
        size_t data_len;

        const uint8_t *bss;
        size_t bss_len;
} Elf32_Sections;

static bool validate_elf(const void *fbytes) {
        const Elf32_EHdr *elf_header = fbytes;

        constexpr uint8_t magic[8] = {0x7f, 0x45, 0x4c, 0x46, 0x01, 0x01, 0x01, 0x00};
        if (memcmp(elf_header->e_ident, magic, 8) != 0) {
                // dprintf(2, "Not an elf file. Magic number did not match.\n");
                return false;
        }

        if (elf_header->e_type != E_TYPE_EXEC && elf_header->e_type != E_TYPE_DYN) {
                // dprintf(2, "File type not supported. Supported types are currently EXEC/DYN only.\n");
                return false;
        }

        if (elf_header->e_machine != E_MACHINE_ARM) {
                // dprintf(2, "Machine not supported, only ARM!\n");
                return false;
        }

        if (elf_header->e_version != 1) {
                // dprintf(2, "Version is not current.\n");
                return false;
        }

        return true;
}

static Elf32_Sections parse_sections(const void *fbytes) {
        Elf32_Sections sections = {0};

        const Elf32_EHdr *elf_header = fbytes;
        const Elf32_SHdr *shdr_table = fbytes + elf_header->e_shoff;
        const Elf32_SHdr *shstrtab = &shdr_table[elf_header->e_shstrndx];

        // printf("\nSection Headers:\n");
        // printf("Indx %-20s Addr   Offs   Size\n", "Section name");
        for (size_t i = 0; i < elf_header->e_shnum; ++i) {
                const Elf32_SHdr *shdr = &shdr_table[i];

                const char *section_name = (char *) (fbytes + shstrtab->sh_offset + shdr->sh_name);
                const uintptr_t section_offset = shdr->sh_offset;
                // printf("[%2zu] %-20s 0x%04x 0x%04x %4i\n", i, section_name,
                // shdr->sh_addr, shdr->sh_offset, shdr->sh_size);


                if (strcmp(section_name, ".text") == 0) {
                        sections.text = (uint8_t *) section_offset;
                        sections.text_len = shdr->sh_size;
                }
                else if (strcmp(section_name, ".data") == 0) {
                        sections.data = (uint8_t *) section_offset;
                        sections.data_len = shdr->sh_size;
                }
                else if (strcmp(section_name, ".rodata") == 0) {
                        sections.rodata = (uint8_t *) section_offset;
                        sections.rodata_len = shdr->sh_size;
                }
                else if (strcmp(section_name, ".bss") == 0) {
                        sections.bss = (uint8_t *) section_offset;
                        sections.bss_len = shdr->sh_size;
                }
                else if (strcmp(section_name, ".interp") == 0) {
                        sections.interp = (const char *) section_offset;
                }
                else if (strcmp(section_name, ".symtab") == 0) {
                        sections.symtab = (Elf32_Sym *) (fbytes + shdr->sh_offset);
                        sections.symtab_len = shdr->sh_size / sizeof(Elf32_Sym);
                }
                else if (strcmp(section_name, ".strtab") == 0) {
                        sections.strtab = (const char *) (fbytes + shdr->sh_offset);
                }
                else if (strcmp(section_name, ".dynsym") == 0) {
                        sections.dynsym = (Elf32_Sym *) (fbytes + shdr->sh_offset);
                        sections.dynsym_len = shdr->sh_size / sizeof(Elf32_Sym);
                }
                else if (strcmp(section_name, ".dynstr") == 0) {
                        sections.dynstr = (const char *) (fbytes + shdr->sh_offset);
                }
                else if (strcmp(section_name, ".rel.plt") == 0) {
                        sections.rel_plt = (Elf32_Rel *) (fbytes + shdr->sh_offset);
                        sections.rel_plt_len = shdr->sh_size / sizeof(Elf32_Rel);
                }
                else if (strcmp(section_name, ".rel.dyn") == 0) {
                        sections.rel_dyn = (Elf32_Rel *) (fbytes + shdr->sh_offset);
                        sections.rel_dyn_len = shdr->sh_size / sizeof(Elf32_Rel);
                }
                else if (strcmp(section_name, ".rel.data") == 0) {
                        sections.rel_data = (Elf32_Rel *) (fbytes + shdr->sh_offset);
                        sections.rel_data_len = shdr->sh_size / sizeof(Elf32_Rel);
                }
                else if (strcmp(section_name, ".rel.bss") == 0) {
                        sections.rel_bss = (Elf32_Rel *) (fbytes + shdr->sh_offset);
                        sections.rel_bss_len = shdr->sh_size / sizeof(Elf32_Rel);
                }
                else if (strcmp(section_name, ".got") == 0) {
                        sections.got = fbytes + shdr->sh_offset;
                        sections.got_len = shdr->sh_size;
                }
                else if (strcmp(section_name, ".got.plt") == 0) {
                        sections.got_plt = fbytes + shdr->sh_offset;
                        sections.got_plt_len = shdr->sh_size;
                }
                else if (strcmp(section_name, ".dynamic") == 0) {
                        sections.dynamic = fbytes + shdr->sh_offset;
                        sections.dynamic_len = shdr->sh_size;
                }
                else if (strcmp(section_name, ".hash") == 0) {
                        sections.hash = fbytes + shdr->sh_offset;
                        sections.hash_len = shdr->sh_size / sizeof(Word);
                }
        }

        return sections;
}

static struct ProcessPage *load_pages_to_ram(const void *fbytes) {
        const Elf32_EHdr *elf_header = fbytes;
        const Elf32_PHdr *phdr_table = fbytes + elf_header->e_phoff;

        unsigned int bufsz = 1024; //this has to change according to phdrs
        unsigned char *buffer = kmalloc(bufsz);
        if (!buffer) {
                return nullptr;
        }
        memset(buffer, 0, bufsz);

        size_t index = 0;
        for (size_t i = 0; i < elf_header->e_phnum; ++i) {
                const Elf32_PHdr *phdr = &phdr_table[i];
                if (phdr->p_type != PT_LOAD) {
                        continue;
                }

                index = phdr->p_paddr;

                if (index + phdr->p_memsz >= bufsz) {
                        bufsz += index + phdr->p_memsz;
                        buffer = krealloc(buffer, bufsz);
                        if (!buffer) {
                                return nullptr;
                        }
                }

                memcpy(buffer + index, fbytes + phdr->p_offset, phdr->p_filesz);
        }

        struct ProcessPage *ppage = kmalloc(sizeof(*ppage));
        ppage->page_ptr = buffer;
        ppage->page_size = bufsz;

        return ppage;
}

enum DynlibStatus {
        OK,

        ERR_NO_STRTAB_FOUND,
        ERR_ONE_DYNLIB_SUPPORTED,
};

static enum DynlibStatus retrieve_dynlib_name(const char **name, const void *fbytes, const Elf32_Sections *sections) {
        const char *strtab = nullptr;

        const Elf32_Dyn *dyn_entry = &sections->dynamic[0];
        int i = 0;
        while (dyn_entry->d_tag != DT_NULL) {
                dyn_entry = &sections->dynamic[i];
                if (dyn_entry->d_tag == DT_STRTAB) {
                        strtab = fbytes + dyn_entry->d_un.d_ptr;
                        break;
                }

                i += 1;
                dyn_entry = &sections->dynamic[i];
        }

        if (!strtab) {
                return ERR_NO_STRTAB_FOUND;
        }


        int names_index = 0;

        i = 0;
        dyn_entry = &sections->dynamic[0];
        while (dyn_entry->d_tag != DT_NULL) {
                if (dyn_entry->d_tag == DT_NEEDED && names_index > 0) {
                        return ERR_ONE_DYNLIB_SUPPORTED;
                }
                if (dyn_entry->d_tag == DT_NEEDED) {
                        *name = strtab + dyn_entry->d_un.d_val;

                        names_index += 1;
                }

                i += 1;
                dyn_entry = &sections->dynamic[i];
        }

        return OK;
}

static unsigned long elf_hash(const unsigned char *name) {
        unsigned long h = 0, g;
        while (*name) {
                h = (h << 4) + *name++;
                if (g = h & 0xf0000000)
                        h ^= g >> 24;
                h &= ~g;
        }
        return h;
}

struct Dynlib {
        const char *name;

        Elf32_Sections sections;
        void *fbytes;
        void *static_base;
        size_t static_base_len;
};

static int load_dynlib(struct Dynlib *dynlib) {
        //todo: fix after implementing normal filesystem - all dynamic libraries must be already in ram

        const struct Process *current_process = scheduler_get_current_process();
        struct Dentry dentry = {
                .name = "initramfs",
        };
        const struct Dentry *initramfs = current_process->root->i_op->lookup(current_process->root, &dentry, 0);
        dentry.name = "lib";
        const struct Dentry *libdir = current_process->root->i_op->lookup(initramfs->inode, &dentry, 0);
        kfree(initramfs);

        dentry.name = dynlib->name;
        struct Dentry *dynlib_file = current_process->root->i_op->lookup(libdir->inode, &dentry, 0);
        kfree(libdir);

        const void *fbytes = ((struct RAMFS_Inode *) dynlib_file->inode)->file_begin;
        kfree(dynlib_file);

        const Elf32_Sections sections = parse_sections(fbytes);
        const size_t static_base_len = sections.data_len + sections.dynamic_len +
                                       sections.got_plt_len + sections.bss_len;
        void *static_base = kmalloc(static_base_len);
        if (!static_base) {
                return -ENOMEM;
        }

        dynlib->fbytes = fbytes;
        dynlib->sections = sections;
        dynlib->static_base = static_base;
        dynlib->static_base_len = static_base_len;
        memcpy(static_base, fbytes + (uintptr_t) dynlib->sections.data, dynlib->sections.data_len);
        memset(static_base + dynlib->sections.data_len, 0, static_base_len - dynlib->sections.data_len);
        //fixme: what about .dynamic and .got.plt?

        return 0;
}

static uint32_t get_dyn(const char *symbol_name, const struct Dynlib *dynlib) {
        //todo: fix after implementing normal filesystem - all dynamic libraries must be already in ram

        const unsigned long symbol_hash = elf_hash(symbol_name);
        // bucket[x%nbucket] gives an index, y,
        // If the symbol
        // table entry is not the one desired, chain[y] gives the next symbol table entry with the same hash value.
        // One can follow the chain links until either the selected symbol table entry holds the desired name or
        // the chain entry contains the value STN_UNDEF.

        const Word *hash = dynlib->sections.hash;

        Word nbucket = hash[0];
        Word nchain = hash[1];

        const Word *bucket = &hash[2];
        const Word *chain = &hash[2 + nbucket];

        Word index = bucket[symbol_hash % nbucket];

        while (index != STN_UNDEF) {
                const Elf32_Sym *symbol = &dynlib->sections.dynsym[index];
                const char *name = &dynlib->sections.dynstr[symbol->st_name];

                if (strcmp(name, symbol_name) == 0) {
                        const Address offset = symbol->st_value;

                        return offset;
                }

                index = chain[index];
        }


        return 0x00;
}

static int resolve_dyn_relocations(
        struct ProcessPage *ppage, const Elf32_Sections *sections, const struct Dynlib *dynlib
) {
        unsigned char *buffer = ppage->page_ptr;

        for (size_t i = 0; i < sections->rel_plt_len; ++i) {
                const Elf32_Rel *rel = &sections->rel_plt[i];
                const Elf32_Sym *symbol = &sections->dynsym[ELF32_R_SYM(rel->r_info)];
                const char *symbol_name = &sections->dynstr[symbol->st_name];

                //todo: search in .dynamic section for info about whereabouts of dynamic libraries
                //todo: should one check if it is a function or an object? The readelf shows R_ARM_JUMP_SLOT for functions
                //fixme: symbol value is relative to ...? Sometimes beginning of a file, sometimes .data section...
                const uintptr_t dyn_address_replacement = (uintptr_t) dynlib->fbytes +
                                                          (uintptr_t) dynlib->sections.data +
                                                          get_dyn(symbol_name, dynlib);
                if (!dyn_address_replacement) {
                        return -1; //no entry found
                }

                //For an executable file or a shared object, the offset is the virtual address of the storage
                //unit affected by the relocation.
                memcpy(buffer + rel->r_offset, (char *) &dyn_address_replacement, 4);
        }

        for (size_t i = 0; i < sections->rel_dyn_len; ++i) {
                const Elf32_Rel *rel = &sections->rel_dyn[i];
                const Elf32_Sym *symbol = &sections->dynsym[ELF32_R_SYM(rel->r_info)];
                const char *symbol_name = &sections->dynstr[symbol->st_name];

                //fixme: is it always sb relative?
                const uintptr_t dyn_address_replacement = (uintptr_t) ppage->static_base + get_dyn(symbol_name, dynlib);
                if (!dyn_address_replacement) {
                        return -1; //no entry found
                }

                memcpy(buffer + rel->r_offset, (char *) &dyn_address_replacement, 4);
        }

        return 0;
}

static Address find_entry_point(const void *fbytes, const Elf32_Sections *sections) {
        const Elf32_EHdr *elf_header = fbytes;
        const Elf32_SHdr *shdr_table = fbytes + elf_header->e_shoff;
        const Elf32_SHdr *shstrtab = &shdr_table[elf_header->e_shstrndx];

        Address _start_address = 0;

        for (size_t i = 0; i < sections->symtab_len; ++i) {
                const char *name;
                if (sections->symtab[i].st_name == 0) {
                        const Elf32_SHdr *section = &shdr_table[i];
                        name = (const char *) (fbytes + shstrtab->sh_offset + section->sh_name);
                        continue;
                }
                else {
                        name = sections->strtab + sections->symtab[i].st_name;
                }

                if (strcmp(name, "_start") == 0) {
                        _start_address = sections->symtab[i].st_value;
                }
        }

        return _start_address;
}

struct ProcessPage *load_exec(const void *fbytes) {
        if (!fbytes || !validate_elf(fbytes)) {
                return nullptr;
        }

        const Elf32_Sections sections = parse_sections(fbytes);

        if (!sections.text) {
                // dprintf(2, "ELF file does not contain .text section.\n");
                return nullptr;
        }
        struct ProcessPage *ppage = load_pages_to_ram(fbytes);

        const char *dynlib_name = nullptr;
        const enum DynlibStatus status = retrieve_dynlib_name(&dynlib_name, fbytes, &sections);
        if (status == OK && dynlib_name) {
                struct Dynlib dynlib = {.name = dynlib_name};
                if (load_dynlib(&dynlib) != 0) {
                        return nullptr; //todo: graceful error
                }
                ppage->static_base = dynlib.static_base;
                ppage->static_base_len = dynlib.static_base_len;

                const int res = resolve_dyn_relocations(ppage, &sections, &dynlib);
                if (res != 0) {
                        goto cleanup_err;
                }
        }
        else if (status == OK) {
                ppage->static_base = nullptr;
        } //todo: handle error cases

        const Address entry_point = find_entry_point(fbytes, &sections);
        if (!entry_point) {
                goto cleanup_err;
        }
        ppage->_start_offset = entry_point;

        return ppage;

cleanup_err:
        kfree(ppage->page_ptr);
        kfree(ppage);
        return nullptr;
}
