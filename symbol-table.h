struct symtab {
    char lexeme[256];
    struct symtab *front;
    struct symtab *back;
    int line;
    int counter;
};

typedef struct symtab symtab;
symtab* lookup(char *name);
void insertID(char *name);
void printSymTab(void);
symtab **fillTab(int *len);

// vim: set sw=4 ts=4 sts=4 et:
