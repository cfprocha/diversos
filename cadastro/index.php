<?php
// Conectar com o banco de dados
include("bd/conecta.php");
// Montar HTML
?>
<!DOCTYPE html>
<html lang="pt-br">
<head>
    <meta charset="UTF-8">
    <meta http-equiv="X-UA-Compatible" content="IE=edge">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Lista Clientes</title>
    <link rel="stylesheet" href="css/style.css">
    <script src="js/clientes.js"></script>
</head>
<body>
    <h1>Cadastro de Clientes</h1>
<?php
// Apresentar registros
include("registros.php");
// Apresentar opções
?>
<p>
    <a href="index.php?acao=incluir" class="botoes">Incluir novo</a>
    <a href="index.php?acao=alterar" class="botoes">Alterar registro</a>
    <a href="index.php?acao=excluir" class="botoes">Excluir registro</a>
</p>
<?php
// Receber dados get
if($_GET['acao']){
    $acao = $_GET['acao'];
    switch ($acao){
        case "incluir":
            include("inclui.php");
            break;
        case "alterar":
            include("altera.php");
            break;
        case "excluir":
            include("exclui.php");
            break;
    }
}
// Fechar conecção com o banco
include("bd/desconecta.php");
?>
