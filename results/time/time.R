#!/usr/bin/env Rscript

# install.packages('ggplot2')
library(ggplot2)
# install.packages('scales')
library(scales)
# install.packages('doBy')
library(doBy)

#########################
# Rate trace processing #
#########################
args = commandArgs(trailingOnly=TRUE)
target="1"
target2="4"
target3="5"

data = read.table(paste(target, "txt", sep="."), header=T)
data2 = read.table(paste(target2, "txt", sep="."), header=T)
data3 = read.table(paste(target3, "txt", sep="."), header=T)

nodeNum <- c(10, 25, 50)
meanTime <- c(mean(data3$Time), mean(data2$Time), mean(data$Time))
result <- data.frame(nodeNum, meanTime)

# graph rates on selected nodes in number of incoming interest packets
g.nodes <- ggplot(result) +
  geom_bar(aes (x=nodeNum, y=meanTime*0.001), stat="identity", fill="#56B4E9", width = 8,) +
  geom_text(aes(x=nodeNum, y=meanTime*0.001, label=format(round(meanTime*0.001, 2), nsmall = 2)), vjust=1.6, color="black",
            position = position_dodge(1.1), size=8) +
  geom_line(aes (x=nodeNum, y=meanTime*0.001), size=2, linetype = "solid") +
  geom_point(aes (x=nodeNum, y=meanTime*0.001), size=5) +
  ylab("Time to be Confirmed [second]") +
  xlab("Total Entity Number") +
  scale_x_continuous(breaks=c(10, 25, 50)) +
  theme_linedraw() +
  theme(
    legend.position="none",
    #legend.position="top",
    legend.title = element_text(size = 25, face = "bold"),
    legend.text = element_text(size = 25, face = "bold"),
    axis.text.x = element_text(color = "grey20", size = 25, angle = 0, face = "plain"),
    axis.text.y = element_text(color = "grey20", size = 25, angle = 90, face = "plain"),
    axis.title.x = element_text(color="black", size=25, face="plain"),
    axis.title.y = element_text(color="black", size=25, face="plain")
  )

png(paste(target, "png", sep="."), width=700, height=500)
print(g.nodes)
retval <- dev.off()
g.nodes
