/* very simple hash functionality */

#include "chuid.h"

extern struct htab *htab;

static void
        h_reset(void) {
    
    unsigned int  i;
    struct h_ent  **p;
    
    htab->first_free= 0;
    
    p = htab->hash;
    for (i = htab->mod; i > 0; i--)
        *p++ = NULL;
}

void
        h_init(const unsigned int mod, const unsigned int het_tab_size) {
    
    struct htab    *htab_r;
    
    if ((htab_r = (struct htab *) malloc(sizeof(struct htab) + sizeof(struct h_ent *) * mod)) == NULL) {
            fprintf(stderr, "ERROR: No memory available for hash initialization\n");
            exit(ENOMEM);
        }
    
    if ((htab_r->het_tab = (struct h_ent *) malloc(sizeof(struct h_ent) * het_tab_size)) == NULL) {
            fprintf(stderr, "ERROR: No memory available for hash initialization\n");
            exit(ENOMEM);
        }
    
    htab_r->mod = mod;
    htab_r->het_tab_size = het_tab_size;
    htab = htab_r;
    
    h_reset();
}

static short int
        h_ins(struct htab *ht, const ino_t ino, const dev_t dev) {
    
    struct h_ent  **hp, *ep2, *ep;

    hp = &ht->hash[ino % ht->mod];
    ep2 = *hp;
    
    if (ep2 != NULL) {
        ep = ep2;
        do {
            if (ep->ino == ino && ep->dev == dev)
                return 1;
            ep = ep->c_link;
        } while (ep != NULL);
    }
    ep = *hp = &ht->het_tab[ht->first_free++];
    ep->ino = ino;
    ep->dev = dev;
    ep->c_link = ep2;
    
    return 0;
}

short int
        h_mins(const ino_t ino, const dev_t dev) {
    
    struct htab    *htab_r = htab;
    struct h_ent   *ep;
    unsigned int    mod, i, het_tab_size;
    
    if (htab_r->first_free>= htab_r->het_tab_size) {
        
        if ((htab_r = (struct htab *) realloc((char *) htab_r, sizeof(struct htab))) == NULL) {
            fprintf(stderr, "ERROR: No memory available for hash calculation\n");
            exit(ENOMEM);
        }
        
        mod = 2 * htab_r->mod;
        het_tab_size = 2 * htab_r->het_tab_size;
        
        if ((htab_r->het_tab = (struct h_ent *) realloc((char *) htab_r->het_tab, sizeof(struct h_ent) * het_tab_size)) == NULL) {
            fprintf(stderr, "ERROR: No memory available for hash calculation\n");
            exit(ENOMEM);
        }
       
        if ((htab_r = (struct htab *) realloc((char *) htab_r, sizeof(struct htab) + sizeof(struct h_ent *) * mod)) == NULL) {
            fprintf(stderr, "ERROR: No memory available for hash calculation\n");
            exit(ENOMEM);
        }
        
        htab_r->mod = mod;
        htab_r->het_tab_size = het_tab_size;
        htab = htab_r;
        
        i = htab_r->first_free;
        
        h_reset();
        
        for (ep = htab_r->het_tab; i > 0; i--) {
            h_ins(htab_r, ep->ino, ep->dev);
            ep++;
        }
    }
    return h_ins(htab_r, ino, dev);
}
