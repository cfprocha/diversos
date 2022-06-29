<?php
echo "<hr><div class='linha'>";
echo "<div class='col-menor'>";
echo "<h1>Exclusão de Clientes</h1>";
echo "</div>";
echo "<div class='col-maior' style='padding: 30px'>";
echo "<form action='excluir.php' method='post'>";
echo "<select name='cliente'>";
$consulta = mysqli_query($conecta, $sql);
while ($linha = mysqli_fetch_array($consulta)) {
    echo "<option>" . $linha['nome'] . "</option>";
}
echo "</select>";
echo "<p>";
echo "<input type='submit' value='Excluir'>";
echo "</p>";
echo "</form>";
echo "</p>";
echo "</div></div>";
?>