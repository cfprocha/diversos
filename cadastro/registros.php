<?php
// Buscar registros do banco
$sql = "select * from cadastro";
$consulta = mysqli_query($conecta, $sql);
// Apresentar registros em tabela
?>
    <div class="linha">
        <div class="col-menor">
            <img src="imgs/clientes.png" alt="clientes">
        </div>
        <div class="col-maior">
            <h2>Clientes Atuais</h2>
            <table>
                <thead>
                    <tr>
                        <td>Nome do Cliente</td>
                        <td>Idade</td>
                    </tr>
                </thead>
                <tbody>
                <?php
                    while ($linha = mysqli_fetch_array($consulta)) {
                ?>
                    <tr>
                        <td>
                            <?php echo $linha['nome']; ?>
                        </td>
                        <td>
                            <?php echo $linha['idade']; ?>
                        </td>
                    </tr>
                    <?php
                    }?>
                </tbody>
            </table>
        </div>
    </div>