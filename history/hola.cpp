#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream.h>

#define COMENTARIO1 '/'
#define COMENTARIO2 '*'
#define CABEZAL_SERVER_HTML "Content-type: text/html\n\n"

void main()
{
		char** variables = new char*[1000];
		fstream a;
		a.open("prueba.html", ios::in);
		char t[1024];
		cout << CABEZAL_SERVER_HTML;

inicio:
		while ( a.getline(t, 1024) != NULL) {
			for (int i = 0; i < int(strlen(t)); i++)
			{
				if (t[i] == '$')
				{
					for (int j = 0; j < 1000; j++) {
						char* test = strstr(t, variables[j]);
						if (test != NULL) 
						{
							if ((strlen(variables[j])+1) == strlen(t))
							{
								cout << variables[j];
								goto inicio;
							}
						}
						else
							goto inicio;
					}
						goto inicio;
				}
				if (t[i] == COMENTARIO1 && t[i+1] == COMENTARIO2)
					goto inicio;
				else
				{
					if (t[i] == '<' && t[i+1] == '%')
					{
						char* pros = new char[i+(strchr(t, '>')-t+1)];
						strcpy(pros, t);
						char* tok = strtok(pros, " ");
						char* aux;
procesar:
						while (tok != NULL)
						{
							if (strcmp(tok, "VAR") == 0)
							{
								aux = new char[strlen(tok)];
								strcpy(aux, tok);
								tok = strtok(NULL, " ");
								goto procesar;
							}
							else
							{
								if (aux != NULL && strcmp(aux, "VAR") == 0) {
									int i = strlen(tok);
									char* var = new char[i+2];
									strcpy(var, tok);
									aux[strlen(tok)+1] = '\0';
									variables[0] = var;
									aux = NULL;
								}
								tok = strtok(NULL, " ");
							}
						}
						goto inicio;
					}
					else
						cout << t[i];
				}
			}
			cout << endl;
		}
 }