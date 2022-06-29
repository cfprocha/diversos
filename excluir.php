<?php
     include("bd/conecta.php");

    $nome = $_POST["cliente"];

    $sql = "delete from cadastro where nome = '${nome}'"; 
    $conecta->query($sql);
    include("bd/desconecta.php");
    header("location: index.php");
    
?>