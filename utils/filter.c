#include <stdio.h>

int main() {
  int c;
  int parcount = 0;
  int beginningofline = 1;
  int printedspace = 0;
  while(1) {
    c = getchar();
    if(beginningofline && c != ' ' && c != '\t')
      beginningofline = 0;
    switch(c) {
      case EOF: goto end;
      case '(': parcount++; goto echo;
      case ')': parcount--; goto echo;
      case ' ':
      case '\t': if(parcount > 0 && beginningofline) { if(printedspace) break; else printedspace = 1; } goto echo;
      case '\n': beginningofline = 1; printedspace = 0; if(parcount > 0) break; else goto echo;
      default: echo:
        putchar(c);
    }
  }
  end:
  putchar('\n');
  return 0;
}
      
    
   
    
