<?php
     include("bd/conecta.php");

    $nome = $_POST["cliente"];
    $campo = $_POST["campo"];
    $novo = $_POST["valor"];

    $sql = "update cadastro set ${campo} = '${novo}' where nome = '${nome}'"; 
    $conecta->query($sql);
    include("bd/desconecta.php");
    header("location: index.php");
        
?>