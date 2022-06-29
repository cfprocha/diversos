<?php
    include("bd/conecta.php");

    $nome = $_POST["nome"];
    $idade = $_POST["idade"];

    $sql = "insert into cadastro (nome,idade) values ('${nome}',${idade})"; 
    $conecta->query($sql);
    include("bd/desconecta.php");
    header("location: index.php");
    
?>