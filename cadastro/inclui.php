<?php 
echo "<hr><div class='linha'>";
echo "<div class='col-menor'>";
echo "<h1>Inclusão de Clientes</h1>";
echo "</div>";
echo "<div class='col-maior' style='padding: 30px'>";
echo "<form action='incluir.php' method='post'>";
echo "<p><label for='nome'>Nome: ";
echo "<input type='text' name='nome' required>";
echo "</label></p>";
echo "<p>";
echo "<label for='idade'>Idade: ";
echo "<input type='number' name='idade' required>";
echo "</label>";
echo "</p>";
echo "<input type='submit' value='Enviar'>";
echo "</form>";
echo "</div></div>";
?>