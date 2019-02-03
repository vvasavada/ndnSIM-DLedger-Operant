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
#target = args[1]
target="30n-10e-15m-500s"

data = read.table(paste(target, "txt", sep="."), header=T)
for (i in 1:nrow(data)) {
  data$ratio[i] = data$unconfirmed[i]/data$total[i]
  if (i == 1)
    data$ratio[i] = 0
}

# graph rates on selected nodes in number of incoming interest packets
g.nodes <- ggplot(data) +
  geom_line(aes (x=time, y=total, colour="darkblue"), size=2, linetype = "dotted") +
  #geom_point(aes (x=time, y=total)) +
  geom_line(aes (x=time, y=unconfirmed, colour="red"), size=2, linetype = "solid") +
  #geom_point(aes (x=time, y=unconfirmed)) +
  scale_color_discrete(name = "Record Types", labels = c("total", "unconfirmed")) +
  xlab("Time [second]") +
  ylab("Records Number") +
  theme_linedraw() +
  theme(
    #legend.position="none", text = element_text(size=12),
    legend.position="top",
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
