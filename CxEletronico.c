#include <stdio.h>
#include <locale.h>
int main(void){
	/*
	Esse código calcula a quantidade de notas de R$ 10, R$ 20 e R$ 50 que devem ser fornecidas
	ao cliente, de acordo com o valor que ele deseja sacar do caixa eletrônico.
	*/
	setlocale(LC_ALL,"");
	// Pergunta o valor do saque
	printf("Qual o valor que você deseja sacar, hoje?\n");
	// Lê o valor digitado
	int varValor,i;
	scanf("%i",&varValor);
	// Cria array bimensional
	// Na primeira dimensão, tenho as notas disponíveis 
	// A segunda será usada para armazenar a quantidade a ser fornecida, de cada nota
	int notas[2][6] = {{100,50,20,10,5,2},{0,0,0,0,0,0}};
	// Faz loop pelo array para verifica a maior nota a ser fornecida para o valor solicitado
	for (i=0;i<5;i++){
		if(varValor >= notas[0][i]){
			// Calcula a quantidade de notas a fornecer
			notas[1][i] = varValor / notas[0][i];
			// Ajusta o valor restante para a próxima passada do loop
			varValor = varValor % notas[0][i];
		}
	}
	// Apresenta os resultados
	printf("Você receberá:\n%i notas de R$ 100\n%i notas de R$ 50\n%i notas de R$ 20\n%i notas de R$ 10\n%i notas de R$ 5\n%i notas de R$ 2",notas[1][0],notas[1][1],notas[1][2],notas[1][3],notas[1][4],notas[1][5]);
	printf("\nA quantia de R$ %i, não pode ser fornecida, pois não temos moedas.",varValor);
}