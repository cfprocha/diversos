# Esse arquivo apresenta uma simples aplicação de lista de tarefas para servidores Linux
# Apesar de ter nomeado o arquivo como .sh ele não se trata de um script, mas sim, de um código a ser inserido diretamente no bash
#
# Antes de usar as funções, tanta a pasta quanto o arquivo precisam ser criados, para tanto use os comandos abaixo
# cd ~ && mkdir Documentos && cd Documentos && touch lst_tarefas.txt && cd ~
#
# Digite: nano .bashrc
# Vá até o final do arquivo e insira as seguintes linhas, para criar uma lista de tarefas
#
## Minha lista de tarefas
#
#Criar variáveis, pseudo BD
export LSTTAR=${HOME}/Documentos/lst_tarefas.txt
#
# Apresenta a lista ou adiciona tarefa nela
lta() { [ $# -eq 0 ] && cat $LSTTAR || echo "$(echo $* | md5sum | cut -c 1-4): $*"  >> $LSTTAR  ;} -> Deixar um espaço de cada lado dentro da chave e dos colchetes
#
# Remover tarefa
ltr() { sed -i "/^$*/d" $LSTTAR ;}
#
# Editar tarefa 
lte() 
{
ltr "$(echo $* | cut -c 1-4)";
lta  "$(echo $* | sed 's/[^ ]* //')" ;}
#
# Assista aqui a um vídeo, demonstrando a criação e uso dela: https://youtu.be/UovKBFpzlWQ
