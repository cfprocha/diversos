<?php
echo "<hr><div class='linha'>";
echo "<div class='col-menor'>";
echo "<h1>Alteração de Clientes</h1>";
echo "</div>";
echo "<div class='col-maior' style='padding: 30px'>";
echo "<form action='alterar.php' method='post'>";
echo "<label for='cliente'>Cliente: </label>";
echo "<select name='cliente'>";
$consulta = mysqli_query($conecta, $sql);
while ($linha = mysqli_fetch_array($consulta)) {
    echo "<option>" . $linha['nome'] . "</option>";
}
echo "</select>";
echo "<label for='campo'>Campo: </label>";
echo "<select name='campo'>";
echo "<option>nome</option>";
echo "<option>idade</option>";
echo "</select>";
echo "<label for='valor'>Novo valor</label>";
echo "<input type='text' name='valor'></input>";
echo "<p>";
echo "<input type='submit' value='Alterar'>";
echo "</p>";
echo "</form>";
echo "</div></div>";
?>