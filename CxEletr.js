// Cria constantes para manipulação
const frm = document.querySelector("form")
const resp1 = document.querySelector("h3")
const resp2 = document.querySelector("h4")

// Cria monitoramento do formulário
frm.addEventListener("submit",(e)=>{
    // Obtém valor
    var valor = Number(frm.oSaque.value)
    // Cria array os valores das notas
    var notas = [100,50,20,10,5,2]
    // Cria variável para o texto a se escrito
    var texto = "O seu saque consistirá de:\n"
    // Faz loop pelo array identificando a nota a ser fornecida
    // bem como a quantidade dela
    for(i=0;i<5;i++){
        if(valor>=notas[i]){
            texto += `${Math.floor(valor / notas[i])} notas de R$ ${notas[i]}\n`
            valor %= notas[i]
        }
    }
    // Atribui os resultados à página
    resp1.innerText = texto
    resp2.innerText = `R$ ${valor} não pode(m) ser fornecido(s).`
    // Evita que o formulário seja enviado
    e.preventDefault()
})