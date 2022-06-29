<?php
// Cria as variáveis
$servidor = "localhost";
$usuario = "gestor";
$senha = "1234";
$banco = "clientes";
// Cria a string de conexão
$conecta = new mysqli($servidor,$usuario,$senha,$banco);
// Testa conexão
if($conecta->connect_error){
    die("Erro de conexão com o Banco de Dados: " . $conecta->connect_error);
}
?>